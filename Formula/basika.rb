class Basika < Formula
  desc "Small IBM BASICA-compatible interpreter clone written in C"
  homepage "https://github.com/kdekorte/basika"
  url "https://github.com/kdekorte/basika/releases/download/v0.99.3/basika-0.99.3.zip"
  sha256 "b58b55178e5d88ddf942ae743ac7da2398381f29655f0060d4be26f52408b025"
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
