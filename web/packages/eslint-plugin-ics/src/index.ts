// Local ESLint rules for ICS TypeScript (ICS-007), loaded by web/eslint.config.js.
import type { TSESLint } from "@typescript-eslint/utils";

import { noRecursion } from "./no-recursion.ts";

const plugin = {
  meta: { name: "@ics/eslint-plugin-ics" },
  rules: { "no-recursion": noRecursion },
} satisfies TSESLint.FlatConfig.Plugin;

export default plugin;
