// Seeded defect (ICS-008) for CodeQL: replace() with a string escapes only the
// first quote (js/incomplete-sanitization). See .github/workflows/codeql.yml.
export function escapeQuotes(text: string): string {
  return text.replace("'", "\\'");
}
