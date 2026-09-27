// ESLint profile for ICS TypeScript (ICS-007): the TypeScript column of the
// coding-standards table in docs/build-plan.md. Every rule is an error, inline
// "eslint" comments have no effect, and "pnpm run lint" allows no warnings.
// policy/check-policy.sh proves each rule still fires on a seeded violation.
import js from "@eslint/js";
import { defineConfig } from "eslint/config";
import reactHooks from "eslint-plugin-react-hooks";
import tseslint from "typescript-eslint";

import ics from "./packages/eslint-plugin-ics/src/index.ts";

const GLOBAL_OBJECT = "/^(window|globalThis|self|global)$/";

export default defineConfig(
  {
    ignores: ["**/node_modules/", "**/dist/", "**/coverage/", "**/playwright-report/", "**/test-results/"],
  },
  // The seeded violations are linted only by policy/check-policy.sh.
  { ignores: ["policy/seeded/"] },
  { linterOptions: { noInlineConfig: true, reportUnusedDisableDirectives: "error" } },
  js.configs.recommended,
  tseslint.configs.strictTypeChecked,
  tseslint.configs.stylisticTypeChecked,
  reactHooks.configs.flat.recommended,
  {
    languageOptions: {
      parserOptions: { projectService: true, tsconfigRootDir: import.meta.dirname },
    },
    plugins: { ics },
    rules: {
      // Simple control flow, no recursion, small functions.
      "ics/no-recursion": "error",
      "max-lines-per-function": ["error", { max: 50, skipBlankLines: false, skipComments: false, IIFEs: true }],
      // Explicit types: no any and no non-null assertion come from strictTypeChecked.
      "@typescript-eslint/explicit-function-return-type": "error",
      // No suppressions: every @ts- directive is banned.
      "@typescript-eslint/ban-ts-comment": [
        "error",
        { "ts-check": false, "ts-expect-error": true, "ts-ignore": true, "ts-nocheck": true },
      ],
      // Block scoping and minimal, immutable global state.
      "no-var": "error",
      "prefer-const": "error",
      "no-param-reassign": ["error", { props: true }],
      "no-extend-native": "error",
      "no-restricted-syntax": [
        "error",
        {
          selector: `AssignmentExpression > MemberExpression.left > Identifier.object[name=${GLOBAL_OBJECT}]`,
          message: "Do not attach state to the global object; keep it in module scope.",
        },
      ],
      // Legacy and unsafe features.
      "no-eval": "error",
      "no-new-func": "error",
    },
  },
  {
    files: ["**/*.js"],
    extends: [tseslint.configs.disableTypeChecked],
  },
);
