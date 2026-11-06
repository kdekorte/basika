class Basika < Formula
  desc "Small IBM BASICA-compatible interpreter clone written in C"
  homepage "https://github.com/kdekorte/basika"
  url "https://github.com/kdekorte/basika/releases/download/v0.99.9/basika-0.99.9.zip"
  sha256 "46d37d4a34c8db4ee0e20674ce6e84378e788d5d9e58e887aad62040791ea321"
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
