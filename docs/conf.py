# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

import os

project = 'minitelnet'
copyright = '2026, Friedel Schön'
author = 'Friedel Schön'
release = '0.1.0'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'sphinxdoc'

extensions = ["breathe"]

# CMake supplies the path to Doxygen's XML output. For manual builds, the
# default matches the standalone Doxyfile when run from the repository root.
breathe_projects = {
    "minitelnet": os.environ.get("MINITELNET_DOXYGEN_XML")
}
breathe_domain_by_extension = {"h": "c", "c": "c"}

breathe_default_project = "minitelnet"
primary_domain = "c"
