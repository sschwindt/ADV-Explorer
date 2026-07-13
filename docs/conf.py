"""Sphinx configuration of the ADV-Explorer documentation."""

project = "ADV-Explorer"
author = "Sebastian Schwindt"
copyright = "2026, Sebastian Schwindt"
release = "0.1.0"

extensions = []

html_theme = "sphinx_rtd_theme"
html_logo = "img/logo.png"
html_favicon = "img/logo.png"
html_theme_options = {
    "logo_only": False,
    "collapse_navigation": False,
}

html_context = {
    "display_github": True,
    "github_user": "sschwindt",
    "github_repo": "ADV-Explorer",
    "github_version": "main",
    "conf_py_path": "/docs/",
}
