from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description="Convert Markdown files to PDF using pandoc."
    )
    parser.add_argument(
        "inputs",
        nargs="*",
        help="Markdown files or directories. Defaults to all *.md files in doc/.",
    )
    parser.add_argument(
        "-o",
        "--output-dir",
        type=Path,
        help="Directory for generated PDFs. Defaults to each source file directory.",
    )
    parser.add_argument(
        "--pdf-engine",
        help="Override the PDF engine passed to pandoc.",
    )
    parser.add_argument(
        "--pandoc",
        default="pandoc",
        help="Path to the pandoc executable.",
    )
    parser.add_argument(
        "--recursive",
        action="store_true",
        help="Search directories recursively for Markdown files.",
    )
    parser.add_argument(
        "--defaults-dir",
        type=Path,
        default=script_dir,
        help=argparse.SUPPRESS,
    )
    return parser.parse_args()


def collect_markdown_files(inputs: list[str], recursive: bool, default_dir: Path) -> list[Path]:
    if not inputs:
        return sorted(default_dir.glob("*.md"))

    files: list[Path] = []
    seen: set[Path] = set()
    pattern = "**/*.md" if recursive else "*.md"

    for raw_input in inputs:
        path = Path(raw_input)
        if path.is_dir():
            candidates = sorted(path.glob(pattern))
        else:
            candidates = [path]

        for candidate in candidates:
            resolved = candidate.resolve()
            if resolved.suffix.lower() != ".md":
                continue
            if resolved not in seen:
                files.append(resolved)
                seen.add(resolved)

    return files


def choose_pdf_engines(requested_engine: str | None) -> list[str]:
    if requested_engine:
        return [requested_engine]

    engines = [
        candidate for candidate in ("pdflatex", "lualatex") if shutil.which(candidate)
    ]
    if engines:
        return engines

    raise SystemExit("No supported PDF engine found. Install lualatex or pdflatex.")


def ensure_pandoc(pandoc: str) -> str:
    resolved = shutil.which(pandoc)
    if not resolved:
        raise SystemExit(f"pandoc not found: {pandoc}")
    return resolved


def build_command(pandoc: str, pdf_engine: str, source: Path, target: Path) -> list[str]:
    return [
        pandoc,
        str(source),
        "--from",
        "markdown",
        "--standalone",
        "--resource-path",
        str(source.parent),
        "--pdf-engine",
        pdf_engine,
        "-V",
        "geometry:margin=1in",
        "-o",
        str(target),
    ]


def main() -> int:
    args = parse_args()
    pandoc = ensure_pandoc(args.pandoc)
    pdf_engines = choose_pdf_engines(args.pdf_engine)
    sources = collect_markdown_files(args.inputs, args.recursive, args.defaults_dir)

    if not sources:
        print("No Markdown files found.", file=sys.stderr)
        return 1

    output_dir = args.output_dir.resolve() if args.output_dir else None
    if output_dir:
        output_dir.mkdir(parents=True, exist_ok=True)

    failures = 0

    for source in sources:
        target = (output_dir / f"{source.stem}.pdf") if output_dir else source.with_suffix(".pdf")
        print(f"{source} -> {target}")

        last_error = "pandoc failed"
        for pdf_engine in pdf_engines:
            command = build_command(pandoc, pdf_engine, source, target)
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode == 0:
                break
            last_error = result.stderr.strip() or f"pandoc failed with {pdf_engine}"
        else:
            failures += 1
            print(last_error, file=sys.stderr)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
