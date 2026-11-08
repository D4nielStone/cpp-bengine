# Editor languages and Lua API reference

The editor ships `en-US` and `pt-BR` catalogs in `commons/assets/locales`.
Choose the default in **Settings → Editor Preferences → Default language** and
press **Apply**, then restart the editor. Navigation preferences remain scoped
to the current session. The language is saved separately in
`editor-preferences.json`, under `BENGINE_INSTALLATION` when supplied by the
installed launcher, or the working directory in development builds.

Each catalog is UTF-8 JSON:

```json
{
  "language": "en-US",
  "strings": {
    "File": "File",
    "Hierarchy": "Hierarchy",
    "Apply": "Apply",
    "api.Transform": "transform.position: world position..."
  }
}
```

Keys are stable source labels, including legacy Portuguese labels such as
`Arquivos`, and `api.<topic>` for the integrated documentation. Copy a shipped
catalog to `<language-code>.json`, retain its keys, and translate its values.
Catalog filenames are discovered by Preferences; codes contain letters, digits,
hyphens or underscores. Missing keys fall back to English, then the source key.
Invalid catalogs are logged and ignored as a whole. Invalid preference files
are preserved and reported on save. Other preference fields are preserved.
User project names, code, paths and API identifiers are never translated.

The catalogs cover the main menus, dock titles, common actions, preferences,
animation controls and the built-in API reference. Some detailed inspector
labels, diagnostic messages, import options and existing guides still use their
original language; add their UI calls to `tr()` and supply both translations
when extending coverage. This does not translate the Markdown files in `docs/`.

**API Reference** in the top bar and document editor opens an offline, scrollable
quick reference. Type a symbol or topic in the search field and press Enter.
Descriptions come from the current JSON catalog; Lua names stay unchanged.
The complete contracts remain in `docs/lua-api/` and `docs/HUD.md`.

Dock layouts retain their legacy canonical titles during save/load so switching
languages does not change saved panel identities. New panels must be added to
`canonical_layout_titles` if their title is translated.

Lua completion now includes global engine symbols, Color, HUD/HudElement and
camera fog/skybox fields. It ignores quoted strings, long strings/comments with
arbitrary `=` delimiters, and assignments inside comments or strings. Results
are cached until the buffer/caret changes. It remains a static API catalog,
not a complete Lua scope/type analyzer; dynamic expressions and arbitrary alias
chains are not inferred.
