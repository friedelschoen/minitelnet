# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

import os

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'minitelnet'
copyright = '2026, Friedel Schön'
author = 'Friedel Schön'
release = 'v0.1.0'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    'sphinx.ext.graphviz',
    'breathe',
]

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']


# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'sphinxdoc'

# -- Breathe Options ----------------------------------------------------------
# https://breathe.readthedocs.io/en/latest/directives.html

breathe_domain_by_extension = {
    "h": "c",
}

breathe_projects = {
    "minitelnet": os.environ.get("MINITELNET_DOXYGEN_XML")
}

breathe_default_project = 'minitelnet'
