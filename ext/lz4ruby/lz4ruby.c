#include <ruby.h>
#include "lz4.h"
#include "lz4hc.h"

typedef int (*CompressFunc)(const char *source, char *dest, int isize);
typedef int (*CompressLimitedOutputFunc)(const char* source, char* dest, int inputSize, int maxOutputSize);

static VALUE lz4internal;
static VALUE lz4_error;
static VALUE lz4_rse;
static VALUE lz4_rsd;

/**
 * LZ4Internal functions.
 */
static VALUE compress_internal(CompressFunc compressor, VALUE header, VALUE input, VALUE in_size) {
  const char *src_p;
  int src_size;

  const char *header_p;
  int header_size;

  VALUE result;
  char *buf;
  int buf_size;

  int comp_size;

  Check_Type(input, T_STRING);
  src_p = RSTRING_PTR(input);
  src_size = NUM2INT(in_size);
  buf_size = LZ4_compressBound(src_size);

  Check_Type(header, T_STRING);
  header_p = RSTRING_PTR(header);
  header_size = RSTRING_LEN(header);

  result = rb_str_new(NULL, buf_size + header_size);
  buf = RSTRING_PTR(result);

  memcpy(buf, header_p, header_size);

  comp_size = compressor(src_p, buf + header_size, src_size);
  rb_str_resize(result, comp_size + header_size);

  return result;
}

static VALUE lz4internal_compress(VALUE self, VALUE header, VALUE input, VALUE in_size) {
  return compress_internal(LZ4_compress, header, input, in_size);
}

static VALUE lz4internal_compressHC(VALUE self, VALUE header, VALUE input, VALUE in_size) {
  return compress_internal(LZ4_compressHC, header, input, in_size);
}

static VALUE lz4internal_uncompress(VALUE self, VALUE input, VALUE in_size, VALUE offset, VALUE out_size) {
  const char *src_p;
  int src_size;

  int header_size;

  VALUE result;
  char *buf;
  int buf_size;

  int read_bytes;

  Check_Type(input, T_STRING);
  src_p = RSTRING_PTR(input);
  src_size = NUM2INT(in_size);

  header_size = NUM2INT(offset);
  buf_size = NUM2INT(out_size);

  result = rb_str_new(NULL, buf_size);
  buf = RSTRING_PTR(result);

  read_bytes = LZ4_uncompress_unknownOutputSize(src_p + header_size, buf, src_size - header_size, buf_size);
  if (read_bytes < 0) {
    rb_raise(lz4_error, "Compressed data is maybe corrupted.");
  }

  return result;
}

static inline void lz4internal_raw_compress_scanargs(int argc, VALUE *argv, VALUE *src, VALUE *dest, size_t *srcsize, size_t *maxsize) {
  switch (argc) {
  case 1:
    *src = argv[0];
    Check_Type(*src, RUBY_T_STRING);
    *srcsize = RSTRING_LEN(*src);
    *dest = rb_str_buf_new(0);
    *maxsize = LZ4_compressBound(*srcsize);
    break;
  case 2:
    *src = argv[0];
    Check_Type(*src, RUBY_T_STRING);
    *srcsize = RSTRING_LEN(*src);
    if (TYPE(argv[1]) == T_STRING) {
      *dest = argv[1];
      *maxsize = LZ4_compressBound(*srcsize);
    } else {
      *dest = rb_str_buf_new(0);
      *maxsize = NUM2SIZET(argv[1]);
    }
    break;
  case 3:
    *src = argv[0];
    Check_Type(*src, RUBY_T_STRING);
    *srcsize = RSTRING_LEN(*src);
    *dest = argv[1];
    Check_Type(*dest, RUBY_T_STRING);
    *maxsize = NUM2SIZET(argv[2]);
    break;
  default:
    //rb_error_arity(argc, 1, 3);
    rb_scan_args(argc, argv, "12", NULL, NULL, NULL);
    // the following code is used to eliminate compiler warnings.
    *src = *dest = 0;
    *srcsize = *maxsize = 0;
  }
}

static inline VALUE lz4internal_raw_compress_common(int argc, VALUE *argv, VALUE lz4, CompressLimitedOutputFunc compressor) {
  VALUE src, dest;
  size_t srcsize;
  size_t maxsize;

  lz4internal_raw_compress_scanargs(argc, argv, &src, &dest, &srcsize, &maxsize);

  if (srcsize > LZ4_MAX_INPUT_SIZE) {
    rb_raise(lz4_error,
             "input size is too big for lz4 compress (max %u bytes)",
             LZ4_MAX_INPUT_SIZE);
  }
  rb_str_modify(dest);
  rb_str_resize(dest, maxsize);
  rb_str_set_len(dest, 0);

  int size = compressor(RSTRING_PTR(src), RSTRING_PTR(dest), srcsize, maxsize);
  if (size < 0) {
    rb_raise(lz4_error, "failed LZ4 raw compress");
  }

  rb_str_resize(dest, size);
  rb_str_set_len(dest, size);

  return dest;
}

/*
 * call-seq:
 *  (compressed string data)  raw_compress(src)
 *  (compressed string data)  raw_compress(src, max_dest_size)
 *  (dest with compressed string data)  raw_compress(src, dest)
 *  (dest with compressed string data)  raw_compress(src, dest, max_dest_size)
 */
static VALUE lz4internal_raw_compress(int argc, VALUE *argv, VALUE lz4i) {
  return lz4internal_raw_compress_common(argc, argv, lz4i, LZ4_compress_limitedOutput);
}

/*
 * call-seq:
 *  (compressed string data)  raw_compressHC(src)
 *  (compressed string data)  raw_compressHC(src, max_dest_size)
 *  (dest with compressed string data)  raw_compressHC(src, dest)
 *  (dest with compressed string data)  raw_compressHC(src, dest, max_dest_size)
 */
static VALUE lz4internal_raw_compressHC(int argc, VALUE *argv, VALUE lz4i) {
  return lz4internal_raw_compress_common(argc, argv, lz4i, LZ4_compressHC_limitedOutput);
}

enum {
  LZ4RUBY_UNCOMPRESS_MAXSIZE = 1 << 24, // tentative value
};

static inline void lz4internal_raw_uncompress_scanargs(int argc, VALUE *argv, VALUE *src, VALUE *dest, size_t *maxsize) {
  switch (argc) {
  case 1:
    *src = argv[0];
    Check_Type(*src, RUBY_T_STRING);
    *dest = rb_str_buf_new(0);
    *maxsize = LZ4RUBY_UNCOMPRESS_MAXSIZE;
    break;
  case 2:
    *src = argv[0];
    Check_Type(*src, RUBY_T_STRING);
    *dest = argv[1];
    if (TYPE(*dest) == T_STRING) {
      *maxsize = LZ4RUBY_UNCOMPRESS_MAXSIZE;
    } else {
      *maxsize = NUM2SIZET(*dest);
      *dest = rb_str_buf_new(0);
    }
    break;
  case 3:
    *src = argv[0];
    Check_Type(*src, RUBY_T_STRING);
    *dest = argv[1];
    Check_Type(*dest, RUBY_T_STRING);
    *maxsize = NUM2SIZET(argv[2]);
    break;
  default:
    //rb_error_arity(argc, 2, 3);
    rb_scan_args(argc, argv, "21", NULL, NULL, NULL);
    // the following code is used to eliminate compiler warnings.
    *src = *dest = 0;
    *maxsize = 0;
  }
}

/*
 * call-seq:
 *  (uncompressed string data)  raw_uncompress(src, max_dest_size = 1 << 24)
 *  (dest for uncompressed string data)  raw_uncompress(src, dest, max_dest_size = 1 << 24)
 */
static VALUE lz4internal_raw_uncompress(int argc, VALUE *argv, VALUE lz4i) {
  VALUE src, dest;
  size_t maxsize;
  lz4internal_raw_uncompress_scanargs(argc, argv, &src, &dest, &maxsize);

  rb_str_modify(dest);
  rb_str_resize(dest, maxsize);
  rb_str_set_len(dest, 0);

  int size = LZ4_decompress_safe(RSTRING_PTR(src), RSTRING_PTR(dest), RSTRING_LEN(src), maxsize);
  if (size < 0) {
    rb_raise(lz4_error, "failed LZ4 raw uncompress at %d", -size);
  }

  rb_str_resize(dest, size);
  rb_str_set_len(dest, size);

  return dest;
}

/*
 * lz4 raw stream encoder
 *
 * RSE -> raw stream encoder
 */

enum {
  LZ4RUBY_RAWSTREAM_PREFIX_SIZE = 64 * 1024,
  LZ4RUBY_RAWSTREAM_BUFFER_MINSIZE = 192 * 1024,
};

struct lz4internal_rse_lz4traits_t {
  void *(*create)(const char *);
  int (*free)(void *);
  int (*update)(void *, const char *, char *, int, int);
  char *(*slide)(void *);
  int (*reset)(void *, const char *);
};

static struct lz4internal_rse_lz4traits_t lz4internal_rse_lz4encode = {
  LZ4_create,
  LZ4_free,
  LZ4_compress_limitedOutput_continue,
  LZ4_slideInputBuffer,
  LZ4_resetStreamState,
};

static struct lz4internal_rse_lz4traits_t lz4internal_rse_lz4encode_hc = {
  LZ4_createHC,
  LZ4_freeHC,
  LZ4_compressHC_limitedOutput_continue,
  LZ4_slideInputBufferHC,
  LZ4_resetStreamStateHC,
};

struct lz4internal_rse_t {
  void *lz4; /* LZ4_create* */
  struct lz4internal_rse_lz4traits_t *traits;
  char *inoff; /* current offset of input buffer */
  char *intail; /* tail of input buffer */
  size_t blocksize;
  VALUE buffer; /* entity of input buffer */
  VALUE predict; /* preset-dictionary / TODO: IMPLEMENT ME! */
  VALUE ishc; /* zero => not hc / non zero => hc */
};

static inline void lz4internal_rse_uninitialized_error(VALUE lz4) {
  rb_raise(rb_eTypeError,
           "not initialized stream encoder - #<%s:%p>",
           rb_obj_classname(lz4), (void *)lz4);
}

static inline struct lz4internal_rse_t *lz4internal_rse_getcontext(VALUE lz4) {
  struct lz4internal_rse_t *lz4p;
  Data_Get_Struct(lz4, struct lz4internal_rse_t, lz4p);
  return lz4p;
}

static void lz4internal_rse_free(void *p) {
  struct lz4internal_rse_t *lz4p = (struct lz4internal_rse_t *)p;
  if (lz4p) {
    if (lz4p->lz4 && lz4p->traits) {
      lz4p->traits->free(lz4p->lz4);
    }
    free(lz4p);
  }
}

static void lz4internal_rse_mark(void *p) {
  struct lz4internal_rse_t *lz4p = (struct lz4internal_rse_t *)p;
  if (lz4p) {
    rb_gc_mark(lz4p->buffer);
    rb_gc_mark(lz4p->predict);
  }
}

static VALUE lz4internal_rse_alloc(VALUE klass) {
  struct lz4internal_rse_t *lz4p;
  VALUE lz4 = Data_Make_Struct(klass, struct lz4internal_rse_t, lz4internal_rse_mark, lz4internal_rse_free, lz4p);
  lz4p->lz4 = NULL;
  lz4p->traits = NULL;
  lz4p->inoff = lz4p->intail = NULL;
  lz4p->blocksize = 0;
  lz4p->ishc = Qfalse;
  lz4p->buffer = Qnil;
  lz4p->predict = Qnil;
  return lz4;
}

static void lz4internal_rse_init_scanargs(int argc, VALUE argv[], size_t *blocksize, VALUE *ishc, VALUE *predict, int inherit) {
  switch (argc) {
  case 1:
    *blocksize = NUM2SIZET(argv[0]);
    *ishc = Qnil;
    *predict = Qnil;
    break;
  case 2:
    *blocksize = NUM2SIZET(argv[0]);
    *ishc = RTEST(argv[1]);
    *predict = Qnil;
    break;
  case 3:
    *blocksize = NUM2SIZET(argv[0]);
    *ishc = RTEST(argv[1]);
    *predict = argv[2];
    Check_Type(*predict, RUBY_T_STRING);
    break;
  case 0:
    if (inherit) {
      *blocksize = 0;
      *ishc = Qnil;
      *predict = Qnil;
      return;
    }
    /* break through */
  default:
    if (inherit) {
      rb_scan_args(argc, argv, "03", NULL, NULL, NULL);
    } else {
      rb_scan_args(argc, argv, "12", NULL, NULL, NULL);
    }
    rb_bug("not reachable here - %s:%d", __FILE__, __LINE__);
    break;
  }

  if (*blocksize > LZ4_MAX_INPUT_SIZE) {
    rb_raise(rb_eArgError, "blocksize is too big");
  }
  if (*blocksize < 1) {
    rb_raise(rb_eArgError, "blocksize is too small");
  }
  *blocksize += LZ4RUBY_RAWSTREAM_PREFIX_SIZE;
  if (*blocksize < LZ4RUBY_RAWSTREAM_BUFFER_MINSIZE) {
    *blocksize = LZ4RUBY_RAWSTREAM_BUFFER_MINSIZE;
  }
}

static void lz4internal_rse_set_predict(struct lz4internal_rse_t *lz4p, VALUE predict) {
  size_t srcsize = RSTRING_LEN(predict);
  size_t maxsize = LZ4_compressBound(LZ4RUBY_RAWSTREAM_PREFIX_SIZE);
  VALUE temp = rb_str_buf_new(maxsize);
  char *predictp = RSTRING_PTR(predict);
  if (srcsize < LZ4RUBY_RAWSTREAM_PREFIX_SIZE) {
    size_t left = LZ4RUBY_RAWSTREAM_PREFIX_SIZE - srcsize;
    memset(lz4p->inoff, 0, left);
    memcpy(lz4p->inoff + left, predictp, srcsize);
  } else {
    size_t left = srcsize - LZ4RUBY_RAWSTREAM_PREFIX_SIZE;
    memcpy(lz4p->inoff, predictp + left, LZ4RUBY_RAWSTREAM_PREFIX_SIZE);
  }
  int size = lz4p->traits->update(lz4p->lz4, lz4p->inoff, RSTRING_PTR(temp), LZ4RUBY_RAWSTREAM_PREFIX_SIZE, maxsize);
  if (size <= 0) { rb_raise(lz4_error, "failed LZ4_compress_limitedOutput_continue"); }
}

static void lz4internal_rse_create_encoder(struct lz4internal_rse_t *lz4p, VALUE buf, VALUE predict) {
  if (lz4p->ishc) {
    lz4p->traits = &lz4internal_rse_lz4encode_hc;
  } else {
    lz4p->traits = &lz4internal_rse_lz4encode;
  }
  lz4p->inoff = RSTRING_PTR(buf);
  lz4p->intail = lz4p->inoff + rb_str_capacity(buf);
  lz4p->blocksize = rb_str_capacity(buf) - LZ4RUBY_RAWSTREAM_PREFIX_SIZE;
  lz4p->lz4 = lz4p->traits->create(lz4p->inoff);
  if (!lz4p->lz4) { rb_raise(lz4_error, "lz4 stream context is not allocated"); }
  if (!NIL_P(predict)) {
    lz4internal_rse_set_predict(lz4p, predict);
  }
}

/*
 * call-seq:
 *    initialize(blocksize)
 *    initialize(blocksize, is_high_compress)
 *    initialize(blocksize, is_high_compress, preset_dictionary)
 */
static VALUE lz4internal_rse_init(int argc, VALUE argv[], VALUE lz4) {
  size_t blocksize;
  VALUE ishc, predict;
  lz4internal_rse_init_scanargs(argc, argv, &blocksize, &ishc, &predict, 0);
  struct lz4internal_rse_t *lz4p = lz4internal_rse_getcontext(lz4);
  if (lz4p->lz4) {
    rb_raise(rb_eTypeError,
             "already initialized - #<%s:%p>",
             rb_obj_classname(lz4), (void *)lz4);
  }
  lz4p->buffer = rb_str_buf_new(blocksize);
  lz4p->ishc = RTEST(ishc);
  lz4internal_rse_create_encoder(lz4p, lz4p->buffer, predict);

  return lz4;
}

/*
 * call-seq:
 *    update(src) -> compress_data
 *    update(src, maxsize) -> compress_data
 *    update(src, dest) -> dest_with_compress_data
 *    update(src, dest, maxsize) -> dest_with_compress_data
 */
static VALUE lz4internal_rse_update(int argc, VALUE argv[], VALUE lz4) {
  VALUE src, dest;
  size_t srcsize, maxsize;
  lz4internal_raw_compress_scanargs(argc, argv, &src, &dest, &srcsize, &maxsize);
  struct lz4internal_rse_t *lz4p = lz4internal_rse_getcontext(lz4);

  rb_str_modify(dest);
  rb_str_set_len(dest, 0);
  rb_str_resize(dest, maxsize);

  VALUE buf = lz4p->buffer;
  Check_Type(buf, RUBY_T_STRING);
  if (rb_str_capacity(buf) < srcsize + LZ4RUBY_RAWSTREAM_PREFIX_SIZE) {
    rb_raise(lz4_error, "detect buffer overflow operation - src is more than block size");
  }

  if (lz4p->intail - lz4p->inoff < (ssize_t)srcsize) {
    lz4p->inoff = lz4p->traits->slide(lz4p->lz4);
  }
  memcpy(lz4p->inoff, RSTRING_PTR(src), srcsize);
  int size = lz4p->traits->update(lz4p->lz4, lz4p->inoff, RSTRING_PTR(dest), srcsize, maxsize);
  if (size <= 0) { rb_raise(lz4_error, "failed LZ4_compress_limitedOutput_continue"); }
  lz4p->inoff += srcsize;
  rb_str_set_len(dest, size);
  rb_str_resize(dest, size);

  return dest;
}

static void lz4internal_rse_reset_state(struct lz4internal_rse_t *lz4p) {
  lz4p->inoff = RSTRING_PTR(lz4p->buffer);
  lz4p->intail = lz4p->inoff + rb_str_capacity(lz4p->buffer);
  lz4p->blocksize = rb_str_capacity(lz4p->buffer) - LZ4RUBY_RAWSTREAM_PREFIX_SIZE;
  memset(lz4p->inoff, 0, LZ4RUBY_RAWSTREAM_BUFFER_MINSIZE);
  int status = lz4p->traits->reset(lz4p->lz4, lz4p->inoff);
  if (status != 0) { rb_raise(lz4_error, "failed reset raw stream encoder"); }
}

/*
 * call-seq:
 *    reset -> self
 *    reset(blocksize) -> self
 *    reset(blocksize, is_high_compress) -> self
 *    reset(blocksize, is_high_compress, presetdictionary) -> self
 *
 * Reset raw stream encoder.
 */
static VALUE lz4internal_rse_reset(int argc, VALUE argv[], VALUE lz4) {
  size_t blocksize;
  VALUE is_high_compress, predict;
  lz4internal_rse_init_scanargs(argc, argv, &blocksize, &is_high_compress, &predict, 1);
  struct lz4internal_rse_t *lz4p = lz4internal_rse_getcontext(lz4);

  if (blocksize == 0) {
    lz4internal_rse_reset_state(lz4p);
  } else if (NIL_P(is_high_compress)) {
    VALUE buf = lz4p->buffer;
    if (rb_str_capacity(buf) != blocksize) {
      rb_str_set_len(buf, 0);
      rb_str_resize(buf, blocksize);
    }
    lz4internal_rse_reset_state(lz4p);
  } else {
    VALUE buf = lz4p->buffer;
    if (rb_str_capacity(buf) != blocksize) {
      rb_str_set_len(buf, 0);
      rb_str_resize(buf, blocksize);
    }
    is_high_compress = RTEST(is_high_compress);
    if (lz4p->ishc == is_high_compress) {
      lz4internal_rse_reset_state(lz4p);
    } else {
      lz4p->traits->free(lz4p->lz4);
      lz4p->lz4 = NULL;
      lz4p->ishc = is_high_compress;
      lz4internal_rse_create_encoder(lz4p, buf, predict);
    }
  }

  return lz4;
}

/*
 * lz4 raw stream decoder
 */

/* RSD: raw stream decoder */
#define LZ4INTERNAL_RSD_PREFIX(lz4) (RSTRUCT_PTR((lz4))[0])

static VALUE lz4internal_rsd_prefix_safe(VALUE lz4) {
  VALUE prefix = LZ4INTERNAL_RSD_PREFIX(lz4);
  if (prefix == Qnil) {
    rb_raise(lz4_error, "not initialized object - #<%s:%p>", rb_obj_classname(lz4), (void *)lz4);
  }
  return prefix;
}

static VALUE lz4internal_rsd_alloc(VALUE klass) {
  VALUE lz4 = rb_struct_alloc_noinit(klass);
  LZ4INTERNAL_RSD_PREFIX(lz4) = Qnil;
  return lz4;
}

static void lz4internal_rsd_init_scanargs(int argc, VALUE argv[], VALUE *prefix) {
  switch (argc) {
  case 0:
    *prefix = Qundef;
    return;
  case 1:
    *prefix = argv[0];
    if (!NIL_P(*prefix)) {
      Check_Type(*prefix, RUBY_T_STRING);
    }
    return;
  default:
    rb_scan_args(argc, argv, "01", NULL);
    rb_bug("not reachable here - %s:%d", __FILE__, __LINE__);
  }
}

static void lz4internal_rsd_setpredict(char *predict, VALUE newpredict) {
  ssize_t size = RSTRING_LEN(newpredict);
  char *newpredictp = RSTRING_PTR(newpredict);
  if (size < LZ4RUBY_RAWSTREAM_PREFIX_SIZE) {
    size_t left = LZ4RUBY_RAWSTREAM_PREFIX_SIZE - size;
    memset(predict, 0, left);
    memcpy(predict + left, newpredictp, size);
  } else {
    newpredictp += size - LZ4RUBY_RAWSTREAM_PREFIX_SIZE;
    memcpy(predict, newpredictp, LZ4RUBY_RAWSTREAM_PREFIX_SIZE);
  }
}

/*
 * call-seq:
 *    initialize
 *    initialize(preset_dictionary)
 */
static VALUE lz4internal_rsd_init(int argc, VALUE argv[], VALUE lz4) {
  VALUE predict;
  lz4internal_rsd_init_scanargs(argc, argv, &predict);

  if (!NIL_P(LZ4INTERNAL_RSD_PREFIX(lz4))) {
    rb_raise(rb_eTypeError,
             "already initialized - #<%s:%p>",
             rb_obj_classname(lz4), (void *)lz4);
  }

  VALUE buf = rb_str_buf_new(LZ4RUBY_RAWSTREAM_PREFIX_SIZE);
  LZ4INTERNAL_RSD_PREFIX(lz4) = buf;

  if (predict != Qundef && predict != Qnil) {
    lz4internal_rsd_setpredict(RSTRING_PTR(buf), predict);
  }

  return lz4;
}

/*
 * call-seq:
 *    update(src) -> uncompressed_data
 *    update(src, maxsize) -> uncompressed_data
 *    update(src, dest) -> dest_with_uncompressed_data
 *    update(src, dest, maxsize) -> dest_with_uncompressed_data
 */
static VALUE lz4internal_rsd_update(int argc, VALUE argv[], VALUE lz4) {
  VALUE src, dest;
  size_t maxsize;
  lz4internal_raw_uncompress_scanargs(argc, argv, &src, &dest, &maxsize);

  rb_str_modify(dest);
  rb_str_set_len(dest, 0);
  rb_str_resize(dest, maxsize + LZ4RUBY_RAWSTREAM_PREFIX_SIZE);

  char *destp = RSTRING_PTR(dest);
  char *prefixp = RSTRING_PTR(lz4internal_rsd_prefix_safe(lz4));
  memcpy(destp, prefixp, LZ4RUBY_RAWSTREAM_PREFIX_SIZE);
  char *destpx = destp + LZ4RUBY_RAWSTREAM_PREFIX_SIZE;
  int size = LZ4_decompress_safe_withPrefix64k(RSTRING_PTR(src), destpx, RSTRING_LEN(src), maxsize);
  if (size < 0) {
    rb_raise(lz4_error, "failed LZ4_decompress_safe_withPrefix64k - maxsize is too small, or corrupt compressed data");
  }
  memcpy(prefixp, destp + size, LZ4RUBY_RAWSTREAM_PREFIX_SIZE);
  memmove(destp, destpx, size);
  rb_str_set_len(dest, size);
  rb_str_resize(dest, size);

  return dest;
}

/*
 * call-seq:
 *    reset -> self
 *    reset(predict) -> self
 */
static VALUE lz4internal_rsd_reset(int argc, VALUE argv[], VALUE lz4) {
  VALUE newpredict;
  lz4internal_rsd_init_scanargs(argc, argv, &newpredict);
  char *predict = RSTRING_PTR(lz4internal_rsd_prefix_safe(lz4));

  if (newpredict == Qundef || newpredict == Qnil) {
    memset(predict, 0, LZ4RUBY_RAWSTREAM_PREFIX_SIZE);
  } else {
    lz4internal_rsd_setpredict(predict, newpredict);
  }

  return lz4;
}

void Init_lz4ruby(void) {
  lz4internal = rb_define_module("LZ4Internal");

  rb_define_module_function(lz4internal, "compress", lz4internal_compress, 3);
  rb_define_module_function(lz4internal, "compressHC", lz4internal_compressHC, 3);
  rb_define_module_function(lz4internal, "uncompress", lz4internal_uncompress, 4);

  rb_define_module_function(lz4internal, "raw_compress", lz4internal_raw_compress, -1);
  rb_define_module_function(lz4internal, "raw_compressHC", lz4internal_raw_compressHC, -1);
  rb_define_module_function(lz4internal, "raw_uncompress", lz4internal_raw_uncompress, -1);

  lz4_error = rb_define_class_under(lz4internal, "Error", rb_eStandardError);

  lz4_rse = rb_define_class_under(lz4internal, "RawStreamEncoder", rb_cObject);
  rb_define_alloc_func(lz4_rse, lz4internal_rse_alloc);
  rb_define_method(lz4_rse, "initialize", RUBY_METHOD_FUNC(lz4internal_rse_init), -1);
  rb_define_method(lz4_rse, "update", RUBY_METHOD_FUNC(lz4internal_rse_update), -1);
  rb_define_method(lz4_rse, "reset", RUBY_METHOD_FUNC(lz4internal_rse_reset), -1);

  lz4_rsd = rb_struct_define_without_accessor(NULL, rb_cObject, lz4internal_rsd_alloc, "prefix", NULL);
  rb_const_set(lz4internal, rb_intern("RawStreamDecoder"), lz4_rsd);
  rb_define_method(lz4_rsd, "initialize", RUBY_METHOD_FUNC(lz4internal_rsd_init), -1);
  rb_define_method(lz4_rsd, "update", RUBY_METHOD_FUNC(lz4internal_rsd_update), -1);
  rb_define_method(lz4_rsd, "reset", RUBY_METHOD_FUNC(lz4internal_rsd_reset), -1);
}
