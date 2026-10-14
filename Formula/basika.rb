class Basika < Formula
  desc "Small IBM BASICA-compatible interpreter clone written in C"
  homepage "https://github.com/kdekorte/basika"
  url "https://github.com/kdekorte/basika/releases/download/v0.99.8/basika-0.99.8.zip"
  sha256 "351b206804aeb72fe6a54702ca3d4760f9e4b06a576ec33a89ddf61c679d342d"
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
