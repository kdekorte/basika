class Basika < Formula
  desc "Small IBM BASICA-compatible interpreter clone written in C"
  homepage "https://github.com/kdekorte/basika"
  url "https://github.com/kdekorte/basika/releases/download/v0.99.5/basika-0.99.5.zip"
  sha256 "1e0b396a4aec35efafb96eb106b94cbccd5887110ff1ac360c66a4447e96edb1"
  license "MIT"

  depends_on "pkg-config" => :build
  depends_on "sdl3"
  depends_on "sdl3_image"
  depends_on "sdl3_mixer"
  depends_on "sdl3_ttf"

  def install
    system "make", "CC=#{ENV.cc}", "PREFIX=#{prefix}"
    system "make", "install", "PREFIX=#{prefix}"
  end

  test do
    assert_match "BASIKA", shell_output("#{bin}/basika --version")
  end
end
