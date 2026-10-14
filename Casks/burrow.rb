cask "burrow" do
  version "0.99.8"
  sha256 "cd32f63d2dafab8f8314e3047469c829a800a5fc298a0d6e5daadb4f35edc8c2"

  url "https://github.com/kdekorte/basika/releases/download/v#{version}/Burrow-#{version}.zip"
  name "Burrow"
  desc "Dig Dug-style digging game written in BASIC and run by basika"
  homepage "https://github.com/kdekorte/basika"

  depends_on arch: :arm64
  depends_on macos: :golden_gate

  app "Burrow.app"

  zap trash: [
    "~/Library/Application Support/Burrow",
    "~/Library/Logs/Burrow.log",
  ]

  caveats <<~EOS
    Burrow is signed ad hoc, not notarized by Apple. The first time you open
    it, macOS will say it cannot verify the app: choose Done, then open
    System Settings > Privacy & Security and click "Open Anyway" for Burrow.
  EOS
end
