"""Builds the website: site/ plus every repo document as an HTML page, so the docs never drift from
the repo. Fails on a broken internal link, because a dead link on the site would otherwise be silent.

    python tools/build_site.py _site

Needs markdown-it-py and PyYAML. CLAUDE.md and the forum post scripts are left out on purpose.
"""
import csv, html, posixpath, re, shutil, sys
from pathlib import Path

import yaml
from markdown_it import MarkdownIt

ROOT = Path(__file__).resolve().parent.parent
REPO = "https://github.com/blancodagoat/texoverride"
SITE_URL = "https://blancodagoat.github.io/texoverride"

# kind, repo path, output name, sidebar label
GROUPS = [
    ("Using it", [
        ("md", "README.md", "index.html", "Guide"),
        ("md", "docs/replacing-files.md", "replacing-files.html", "Replacing files"),
        ("md", "COLLECTIONS.md", "collections.html", "Collection names"),
        ("md", "docs/reading-the-log.md", "reading-the-log.html", "Reading the log"),
        ("md", "docs/textures-and-budget.md", "textures-and-budget.html", "Textures and memory"),
        ("md", "docs/antivirus.md", "antivirus.html", "Antivirus"),
        ("text", "tools/tex_overrides_README.txt", "folder-readme.html", "The readme in the zip"),
    ]),
    ("Lookup tables", [
        ("tsv", "docs/overlay_index.tsv", "overlay-index.html", "Tattoos and overlays"),
        ("tsv", "docs/ped_collections.tsv", "ped-collections.html", "Characters in the game"),
    ]),
    ("Under the hood", [
        ("md", "docs/how-it-works.md", "how-it-works.html", "How it works"),
        ("md", "docs/build.md", "build.html", "Building it yourself"),
        ("forms", ".github/ISSUE_TEMPLATE", "report-forms.html", "Report forms"),
        ("yaml", ".github/workflows", "workflows.html", "Build and release workflows"),
        ("md", "docs/client-side-dlc-packs.md", "client-side-dlc-packs.html", "Client side DLC packs"),
    ]),
    ("Project", [
        ("md", "CHANGELOG.md", "changelog.html", "Changelog"),
        ("text", "LICENSE", "license.html", "License"),
    ]),
]
PAGES = [p for _, group in GROUPS for p in group]
OUTPUT_FOR = {path: out for _, path, out, _ in PAGES}
OUTPUT_FOR["docs"] = "index.html"

TSV_INTRO = {
    "docs/overlay_index.tsv": ("Tattoos and overlays",
        "Every tattoo and overlay in the base game: its preset name, the file that owns it, where it "
        "sits on the body and which texture it uses."),
    "docs/ped_collections.tsv": ("Characters in the game",
        "Every ped collection in game build 3751, how many component files it has and which archive it comes from."),
}


def slugify(text, seen):
    """GitHub's heading anchors, so links written for the repo keep working here."""
    slug = re.sub(r"[^\w\- ]", "", text.strip().lower()).replace(" ", "-")
    n = seen.get(slug, 0)
    seen[slug] = n + 1
    return f"{slug}-{n}" if n else slug


def resolve_link(href, source):
    """Turns a link written for the repo into one that works from a page in docs/."""
    if re.match(r"[a-z][a-z0-9+.-]*:", href) or href.startswith("#"):
        if href.startswith(REPO + "/blob/main/"):
            return resolve_link("/" + href[len(REPO + "/blob/main/"):], "")
        return href
    target, _, frag = href.partition("#")
    frag = "#" + frag if frag else ""
    path = target[1:] if target.startswith("/") else posixpath.normpath(posixpath.join(posixpath.dirname(source), target))
    path = path.rstrip("/")
    if path.startswith(".."):    # ../../issues/new/choose climbs out of the file view on GitHub
        return f"{REPO}/{path.lstrip('./')}{frag}"
    if path in OUTPUT_FOR:
        return OUTPUT_FOR[path] + frag
    return f"{REPO}/blob/main/{path}{frag}"


def render_markdown(path):
    md = MarkdownIt("commonmark", {"html": False}).enable(["table", "strikethrough"])
    tokens = md.parse((ROOT / path).read_text(encoding="utf-8"))
    seen, headings, title = {}, [], None
    for i, tok in enumerate(tokens):
        if tok.type == "heading_open":
            text = "".join(c.content for c in tokens[i + 1].children if c.type in ("text", "code_inline"))
            slug = slugify(text, seen)
            tok.attrSet("id", slug)
            if tok.tag == "h1" and title is None:
                title = text
            elif tok.tag == "h2":
                headings.append((slug, text))
        for child in tok.children or []:
            if child.type == "link_open":
                child.attrSet("href", resolve_link(child.attrGet("href"), path))
    body = md.renderer.render(tokens, md.options, {})
    body = body.replace("<table>", '<div class="table-scroll" tabindex="0"><table>').replace("</table>", "</table></div>")
    return title, body, headings


def render_text(path, label):
    text = (ROOT / path).read_text(encoding="utf-8")
    return label, f'<h1>{html.escape(label)}</h1>\n<pre class="plain" tabindex="0"><code>{html.escape(text)}</code></pre>', []


def render_tsv(path):
    title, intro = TSV_INTRO[path]
    rows = list(csv.reader((ROOT / path).read_text(encoding="utf-8").splitlines(), delimiter="\t", quoting=csv.QUOTE_NONE))
    head, rest = rows[0], rows[1:]
    ths = "".join(f'<th scope="col">{html.escape(c)}</th>' for c in head)
    trs = "\n".join("<tr>" + "".join(f"<td>{html.escape(c)}</td>" for c in r) + "</tr>" for r in rest)
    name = Path(path).name
    body = f"""<h1>{html.escape(title)}</h1>
<p>{html.escape(intro)} {len(rest):,} rows. <a href="{name}">Download the table as a file</a>.</p>
<div class="filter" hidden>
  <label for="filter-input">Search this table</label>
  <input id="filter-input" type="search" autocomplete="off" spellcheck="false">
  <p id="filter-count" role="status"></p>
</div>
<div class="table-scroll data" tabindex="0"><table id="data-table"><thead><tr>{ths}</tr></thead><tbody>
{trs}
</tbody></table></div>
<p id="filter-empty" hidden>Nothing in this table matches that search.</p>
<script src="filter.js"></script>"""
    return title, body, []


def inline_md(text):
    md = MarkdownIt("commonmark", {"html": False})
    tokens = md.parse(text or "")
    for tok in tokens:
        for child in tok.children or []:
            if child.type == "link_open":
                child.attrSet("href", resolve_link(child.attrGet("href"), ""))
    return md.renderer.render(tokens, md.options, {})


def raw_source(path):
    text = path.read_text(encoding="utf-8")
    return f'<pre tabindex="0"><code>{html.escape(text)}</code></pre>'


def render_forms(folder):
    d = ROOT / folder
    out = ["<h1>Report forms</h1>",
           "<p>These are the questions GitHub asks when you open an issue. Having the answers ready saves a round of back and forth.</p>"]
    headings = []
    for f in sorted(d.glob("*.yml"), key=lambda f: (f.name != "config.yml", f.name)):
        data = yaml.safe_load(f.read_text(encoding="utf-8"))
        if f.name == "config.yml":
            slug = "before-you-file"
            headings.append((slug, "Before you file"))
            out.append(f'<h2 id="{slug}">Before you file</h2><ul>')
            for link in data.get("contact_links", []):
                out.append(f'<li><a href="{html.escape(resolve_link(link["url"], ""))}">{html.escape(link["name"])}</a>: {html.escape(link["about"])}</li>')
            out.append("</ul>")
        else:
            slug = f.stem.replace("_", "-")
            headings.append((slug, data["name"]))
            out.append(f'<h2 id="{slug}">{html.escape(data["name"])}</h2><p>{html.escape(data["description"])}</p>')
            out.append(f'<p><a class="btn btn-sm btn-primary" href="{REPO}/issues/new?template={f.name}">Open this form on GitHub</a></p>')
            for field in data["body"]:
                attrs = field["attributes"]
                if field["type"] == "markdown":
                    out.append(inline_md(attrs["value"]))
                    continue
                need = "required" if field.get("validations", {}).get("required") else "optional"
                out.append(f'<h3>{html.escape(attrs["label"])} <span class="need">{need}</span></h3>')
                out.append(inline_md(attrs.get("description", "")))
                if attrs.get("options"):
                    out.append("<ul>" + "".join(f"<li>{html.escape(str(o))}</li>" for o in attrs["options"]) + "</ul>")
        out.append(f"<details><summary>{f.name}</summary>{raw_source(f)}</details>")
    return "Report forms", "\n".join(out), headings


def render_workflows(folder):
    out = ["<h1>Build and release workflows</h1>",
           "<p>These files tell GitHub how to build the plugin and this website. Every release comes from the first one, so you can read exactly what was done to make the file you download.</p>"]
    headings = []
    for f in sorted((ROOT / folder).glob("*.yml")):
        slug = f.stem
        headings.append((slug, f.name))
        out.append(f'<h2 id="{slug}">{f.name}</h2>{raw_source(f)}')
    return "Build and release workflows", "\n".join(out), headings


def sidebar(current, headings):
    parts = []
    for group, pages in GROUPS:
        parts.append(f"<p>{html.escape(group)}</p><ul>")
        for _, _, out, label in pages:
            here = out == current
            mark = ' aria-current="page"' if here else ""
            parts.append(f'<li><a href="{out}"{mark}>{html.escape(label)}</a>')
            if here and headings:
                parts.append('<ul class="toc">' + "".join(
                    f'<li><a href="#{s}">{html.escape(t)}</a></li>' for s, t in headings) + "</ul>")
            parts.append("</li>")
        parts.append("</ul>")
    return "".join(parts)


def page(kind, source, out, label, title, body, headings):
    source_url = f"{REPO}/{'tree' if (ROOT / source).is_dir() else 'blob'}/main/{source}"
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark">
<meta name="theme-color" content="#0a0a0a">
<title>{html.escape(title)}: texoverride</title>
<link rel="canonical" href="{SITE_URL}/docs/{out}">
<link rel="icon" href="data:,">
<link rel="stylesheet" href="../style.css">
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
<header class="nav">
  <div class="wrap nav-row">
    <a class="wordmark" href="../index.html">texoverride</a>
    <div class="nav-actions">
      <a class="btn btn-sm btn-ghost nav-github" href="{REPO}">GitHub</a>
      <a class="btn btn-sm btn-primary" href="{REPO}/releases/latest/download/texoverride.zip">Download</a>
    </div>
  </div>
</header>
<div class="wrap docs">
  <details class="side" open>
    <summary>All pages</summary>
    <nav aria-label="Documentation">{sidebar(out, headings)}</nav>
  </details>
  <script>if (matchMedia("(max-width: 959px)").matches) document.querySelector(".side").open = false;</script>
  <main id="main" class="prose">
    <p class="path"><a href="{source_url}">{html.escape(source)}</a></p>
    {body}
  </main>
</div>
<footer class="foot">
  <div class="wrap foot-row">
    <p>Made from the files in the repository, so it always matches them. MIT licensed. Not affiliated with Cfx.re or Rockstar Games.</p>
    <p class="foot-links"><a href="{REPO}">GitHub</a><a href="{REPO}/releases">Releases</a><a href="{REPO}/issues/new/choose">Report a bug</a></p>
  </div>
</footer>
</body>
</html>
"""


def check_links(out_dir):
    """Every internal link has to reach a real file and, if it names one, a real anchor."""
    html_files = list(out_dir.rglob("*.html"))
    ids = {f.resolve(): set(re.findall(r'\bid="([^"]+)"', f.read_text(encoding="utf-8"))) for f in html_files}
    bad = []
    for f in html_files:
        for href in re.findall(r'href="([^"]+)"', f.read_text(encoding="utf-8")):
            href = html.unescape(href)
            if re.match(r"[a-z][a-z0-9+.-]*:", href):
                continue
            target, _, frag = href.partition("#")
            dest = (f.parent / target).resolve() if target else f.resolve()
            if not dest.exists():
                bad.append(f"{f.relative_to(out_dir)}: missing file {href}")
            elif frag and dest.suffix == ".html" and frag not in ids.get(dest, ()):
                bad.append(f"{f.relative_to(out_dir)}: missing anchor {href}")
    return bad


def main():
    out_dir = Path(sys.argv[1]).resolve()
    if out_dir.exists():
        shutil.rmtree(out_dir)
    shutil.copytree(ROOT / "site", out_dir)
    docs = out_dir / "docs"
    docs.mkdir(exist_ok=True)
    shutil.move(str(out_dir / "filter.js"), docs / "filter.js")

    for kind, source, out, label in PAGES:
        if kind == "md":
            title, body, headings = render_markdown(source)
            title = title or label
        elif kind == "text":
            title, body, headings = render_text(source, label)
        elif kind == "tsv":
            title, body, headings = render_tsv(source)
            shutil.copy(ROOT / source, docs / Path(source).name)
        elif kind == "forms":
            title, body, headings = render_forms(source)
        else:
            title, body, headings = render_workflows(source)
        (docs / out).write_text(page(kind, source, out, label, title, body, headings), encoding="utf-8", newline="\n")

    bad = check_links(out_dir)
    print(f"{len(PAGES)} pages in {out_dir}, {len(bad)} broken link(s)")
    for line in bad:
        print("  " + line)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
