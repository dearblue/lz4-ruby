
$:.unshift(File.join(File.dirname(__FILE__), "../lib"))
require "lz4-ruby"
require "digest"

def sha1(src)
  Digest::SHA1.hexdigest(src)
end

describe LZ4Internal::RawStreamEncoder do
  it "new and reset" do
    expect { LZ4Internal::RawStreamEncoder.new(-1) }.to raise_error ArgumentError
    expect { LZ4Internal::RawStreamEncoder.new(1 << 31) }.to raise_error ArgumentError
    expect { LZ4Internal::RawStreamEncoder.new(1 << 20, false, 0) }.to raise_error TypeError
    expect { LZ4Internal::RawStreamEncoder.new(1 << 20, false, :abcdefg) }.to raise_error TypeError
    expect(LZ4Internal::RawStreamEncoder.new(1 << 20, false, "abcdefg")).to be_a_kind_of LZ4Internal::RawStreamEncoder

    srcbase = ?0 * (1 << 20)
    enc = LZ4Internal::RawStreamEncoder.new(srcbase.bytesize)
    expect(enc.reset).to eq enc
    expect(enc.reset(1 << 22)).to eq enc
    expect { enc.reset(-1) }.to raise_error ArgumentError
    expect { enc.reset(1 << 31) }.to raise_error ArgumentError

    enc.reset(1 << 20)
    dest1 = enc.update(srcbase)
    dest2 = enc.update(srcbase)
    enc.reset
    dest3 = enc.update(srcbase)
    expect(sha1(dest3)).to eq sha1(dest1)
    enc.reset(1 << 20, false, srcbase.slice(0, 1 << 16))
    dest4 = enc.update(srcbase)
    expect(sha1(dest4)).to eq sha1(dest2)
  end
end

describe LZ4Internal::RawStreamEncoder, "and", LZ4Internal::RawStreamDecoder do
  describe "(blocksize=262144) (high compression)" do
    before :all do
      @encoder = LZ4Internal::RawStreamEncoder.new(262144, true)
      @decoder = LZ4Internal::RawStreamDecoder.new
      @src = 16384.times.map { |i| i & 0xff }.shuffle.pack("C*") * 16
    end

    it "encode / decode" do
      10.times do |i|
        w = @encoder.update(@src)
        expect(Digest::SHA1.hexdigest(@decoder.update(w))).to eq Digest::SHA1.hexdigest(@src)
      end
    end
  end

  describe "(blocksize=4 MiB)" do
    before :all do
      @encoder = LZ4Internal::RawStreamEncoder.new(1 << 22)
      @decoder = LZ4Internal::RawStreamDecoder.new
      @src = 16384.times.map { |i| i & 0xff }.shuffle.pack("C*") * 256
    end

    it "encode / decode" do
      10.times do |i|
        w = @encoder.update(@src)
        expect(Digest::SHA1.hexdigest(@decoder.update(w))).to eq Digest::SHA1.hexdigest(@src)
      end
    end
  end

  describe "(over blocksize - 4 MiB for 1 MiB blocksize)" do
    before :all do
      @encoder = LZ4Internal::RawStreamEncoder.new(1 << 18)
      @src = 16384.times.map { |i| i & 0xff }.shuffle.pack("C*") * 256
    end

    it "encode / decode" do
      expect { @encoder.update(@src) }.to raise_error LZ4Internal::Error
    end
  end

  describe "stream encoder and stateless decoder" do
    before :all do
      @encoder = LZ4Internal::RawStreamEncoder.new(1 << 20)
      @src = ?0 * (1 << 20)
    end

    it "first encode data is raw decodable" do
      w = @encoder.update(@src)
      expect(Digest::SHA1.hexdigest(LZ4Internal.raw_uncompress(w))).to eq Digest::SHA1.hexdigest(@src)
    end

    it "second encode data is not raw decodable" do
      w = @encoder.update(@src)
      expect { LZ4Internal.raw_uncompress(w) }.to raise_error LZ4Internal::Error
    end
  end
end
