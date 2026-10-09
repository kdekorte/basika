class Basika < Formula
  desc "Small IBM BASICA-compatible interpreter clone written in C"
  homepage "https://github.com/kdekorte/basika"
  url "https://github.com/kdekorte/basika/releases/download/v0.99.7/basika-0.99.7.zip"
  sha256 "e67c0f7a956aa55cb37797434889744ebbc7b039d449b043e192fe2023ceebcf"
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
