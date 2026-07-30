class CodebaseMemoryMcp < Formula
  desc "Fast code intelligence engine for AI coding agents"
  homepage "https://github.com/usman8786/project-mind-mcp"
  version "0.8.1"
  license "MIT"

  on_macos do
    on_arm do
      url "https://github.com/usman8786/project-mind-mcp/releases/download/v#{version}/project-mind-mcp-darwin-arm64.tar.gz"
      sha256 "fbd047509852021b5446a11141bcb0a3d1dcaebf6e5112460960f29f052c1c58"
    end
    on_intel do
      url "https://github.com/usman8786/project-mind-mcp/releases/download/v#{version}/project-mind-mcp-darwin-amd64.tar.gz"
      sha256 "fb62da3016ea12b948351208759b5c083fb1446cf6e78d6db8b7cd28fe86fd54"
    end
  end

  on_linux do
    on_arm do
      url "https://github.com/usman8786/project-mind-mcp/releases/download/v#{version}/project-mind-mcp-linux-arm64.tar.gz"
      sha256 "d2f842d1365da5c35d9c5796f57a821c9745267350994346735e1e6e04d46091"
    end
    on_intel do
      url "https://github.com/usman8786/project-mind-mcp/releases/download/v#{version}/project-mind-mcp-linux-amd64.tar.gz"
      sha256 "dbd3b92ea870ef240b63059f26bda15015f76ef9978931bebc3a0f9d09470973"
    end
  end

  def install
    bin.install "project-mind-mcp"
    # Third-party attribution bundle (present in archives since v0.8.1)
    doc.install "THIRD_PARTY_NOTICES.md" if File.exist?("THIRD_PARTY_NOTICES.md")
  end

  def caveats
    <<~EOS
      Run the following to configure your coding agents:
        project-mind-mcp install

      To tap this formula directly:
        brew tap deusdata/project-mind-mcp https://github.com/usman8786/project-mind-mcp
        brew install project-mind-mcp
    EOS
  end

  test do
    assert_match "project-mind-mcp", shell_output("#{bin}/project-mind-mcp --version")
  end
end
