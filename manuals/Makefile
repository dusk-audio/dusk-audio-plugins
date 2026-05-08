# Makefile for Dusk Audio plugin manuals.
#
# Sources live in this directory. Outputs land in the website repo at
# $(DUSK_WEBSITE_REPO)/assets/manuals/. Default targets:
#
#   make preflight   - lint sources only (no PDF generation, no deps beyond Python)
#   make all         - render combined manual + every per-plugin PDF
#   make combined    - render the combined dusk-audio-manual.pdf only
#   make <slug>      - render a single per-plugin PDF (e.g. make multi-comp)
#   make clean       - remove generated PDFs from the website repo
#
# Requires: python3 + pyyaml for preflight.
# Requires (for PDF generation): pandoc, xelatex (TeX Live with xetex).
#
# Install (pick the right one for your distro):
#   Debian/Ubuntu: sudo apt install pandoc texlive-xetex texlive-fonts-recommended python3-yaml
#   Fedora/RHEL:   sudo dnf install pandoc texlive-xetex texlive-collection-fontsrecommended python3-pyyaml
#   openSUSE:      sudo zypper install pandoc texlive-xetex texlive-collection-fontsrecommended python3-PyYAML
#   macOS:         brew install pandoc && brew install --cask mactex-no-gui
#
# Override the website repo location via env var:
#   make all DUSK_DUSK_WEBSITE_REPO=/path/to/dusk-audio.github.io

DUSK_WEBSITE_REPO ?= $(HOME)/projects/dusk-audio.github.io
OUTPUT_DIR    = $(DUSK_WEBSITE_REPO)/assets/manuals
PYTHON        = python3

# Discover slugs from the markdown files (excluding _template, README).
SLUGS = $(patsubst %.md,%,$(filter-out _%.md README.md,$(notdir $(wildcard *.md))))

.PHONY: all combined preflight clean help $(SLUGS)

all: preflight
	$(PYTHON) build_manuals.py --all --skip-preflight

combined: preflight
	$(PYTHON) build_manuals.py --combined --skip-preflight

preflight:
	$(PYTHON) preflight.py

# Generate per-plugin PDF. Usage: make multi-comp, make 4k-eq, etc.
$(SLUGS): preflight
	$(PYTHON) build_manuals.py --slug $@ --skip-preflight

clean:
	@echo "Removing PDFs from $(OUTPUT_DIR)"
	rm -f $(OUTPUT_DIR)/dusk-audio-manual.pdf
	rm -f $(OUTPUT_DIR)/*-manual.pdf

help:
	@echo "make preflight        # lint markdown sources only (no deps beyond Python)"
	@echo "make all              # combined + every per-plugin PDF"
	@echo "make combined         # combined PDF only"
	@echo "make <slug>           # one per-plugin PDF (e.g. 'make multi-comp')"
	@echo "make clean            # remove generated PDFs"
	@echo
	@echo "Discovered slugs: $(SLUGS)"
	@echo "Output dir:       $(OUTPUT_DIR)"
