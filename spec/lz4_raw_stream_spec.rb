
$:.unshift(File.join(File.dirname(__FILE__), "../lib"))
require "lz4-ruby"
require "digest"

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
