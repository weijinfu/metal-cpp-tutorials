(() => {
  const escapeHtml = (text) =>
    text
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;");

  const stash = (html, pattern, className, tokens) =>
    html.replace(pattern, (match) => {
      const key = `@@TOK${tokens.length}@@`;
      tokens.push(`<span class="${className}">${match}</span>`);
      return key;
    });

  const restore = (html, tokens) =>
    html.replace(/@@TOK(\d+)@@/g, (_, index) => tokens[Number(index)]);

  const highlightCpp = (line) => {
    const tokens = [];
    let html = escapeHtml(line);
    html = stash(html, /#\s*\w+.*/g, "tok-preproc", tokens);
    html = stash(html, /\/\/.*/g, "tok-comment", tokens);
    html = stash(html, /"(?:\\.|[^"\\])*"/g, "tok-string", tokens);
    html = stash(html, /\b(?:0x[0-9a-fA-F]+|\d+(?:\.\d+)?(?:f|u)?)\b/g, "tok-number", tokens);

    html = html.replace(/\b(kernel|vertex|fragment|using|namespace|struct|class|template|typename|public|private|return|if|else|for|while|const|constexpr|static_cast|reinterpret_cast|sizeof|true|false|nullptr|new|delete)\b/g, '<span class="tok-keyword">$1</span>');
    html = html.replace(/\b(MTL|NS|CA|std|uint8_t|uint16_t|uint32_t|size_t|float|float2|float3|float4|float4x4|uint|uint2|uint3|uchar|uchar4|device|constant|thread|texture2d|sampler|bool|void|int|char|auto)\b/g, '<span class="tok-type">$1</span>');
    return restore(html, tokens);
  };

  const highlightCmake = (line) => {
    const tokens = [];
    let html = escapeHtml(line);
    html = stash(html, /#.*/g, "tok-comment", tokens);
    html = stash(html, /"(?:\\.|[^"\\])*"/g, "tok-string", tokens);
    html = stash(html, /\$\{[A-Za-z0-9_]+\}/g, "tok-type", tokens);
    html = html.replace(/\b(add_executable|add_custom_command|add_custom_target|add_dependencies|cmake_minimum_required|project|set|target_include_directories|target_link_libraries|target_compile_definitions|set_target_properties|COMMAND|OUTPUT|DEPENDS|PRIVATE|CACHE|PATH|PROPERTIES)\b/g, '<span class="tok-keyword">$1</span>');
    return restore(html, tokens);
  };

  const highlightShell = (line) => {
    const tokens = [];
    let html = escapeHtml(line);
    html = stash(html, /#.*/g, "tok-comment", tokens);
    html = stash(html, /"(?:\\.|[^"\\])*"/g, "tok-string", tokens);
    html = html.replace(/^(\s*)(cmake|xcrun|\.\/[^\s]+)/, '$1<span class="tok-keyword">$2</span>');
    html = html.replace(/(\s)(-[A-Za-z][A-Za-z-]*)/g, '$1<span class="tok-type">$2</span>');
    return restore(html, tokens);
  };

  const highlighterFor = (code) => {
    if (code.classList.contains("language-cmake")) return highlightCmake;
    if (code.classList.contains("language-sh")) return highlightShell;
    if (code.classList.contains("language-cpp")) return highlightCpp;
    return null;
  };

  const highlightPlainLine = (line, highlight) =>
    `<span class="code-line">${highlight(line)}</span>`;

  const highlightExistingLines = (code, highlight) => {
    const lines = Array.from(code.querySelectorAll(":scope > .code-line"));
    if (lines.length === 0) return false;

    for (const line of lines) {
      if (line.querySelector(".tok-keyword, .tok-type, .tok-string, .tok-number, .tok-preproc, .tok-comment")) {
        continue;
      }
      line.innerHTML = highlight(line.textContent);
    }
    return true;
  };

  const highlightBlock = (code) => {
    const highlight = highlighterFor(code);
    if (!highlight || code.querySelector(".tok-keyword, .tok-type, .tok-string, .tok-number, .tok-preproc, .tok-comment")) {
      return;
    }

    if (highlightExistingLines(code, highlight)) {
      return;
    }

    const lines = code.textContent.replace(/^\n/, "").replace(/\n$/, "").split("\n");
    code.innerHTML = lines.map((line) => highlightPlainLine(line, highlight)).join("");
  };

  window.addEventListener("DOMContentLoaded", () => {
    document.querySelectorAll("pre > code").forEach(highlightBlock);
  });
})();
