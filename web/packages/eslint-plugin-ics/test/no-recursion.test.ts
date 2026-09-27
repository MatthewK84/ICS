import { RuleTester } from "@typescript-eslint/rule-tester";
import { afterAll, describe, expect, it } from "vitest";

import plugin from "../src/index.ts";
import { noRecursion } from "../src/no-recursion.ts";

RuleTester.afterAll = afterAll;
RuleTester.describe = describe;
RuleTester.it = it;

const ruleTester = new RuleTester();

function recursive(name: string): { messageId: "recursive"; data: { name: string } } {
  return { messageId: "recursive", data: { name } };
}

ruleTester.run("no-recursion", noRecursion, {
  valid: [
    "function leaf(): number { return 1; } function root(): number { return leaf() + leaf(); }",
    // Shadowing: the inner name refers to the parameter, not the function.
    "function apply(apply: () => void): void { apply(); }",
    // Calls to things this file does not define.
    "function log(): void { console.log('x'); Math.max(1, 2); }",
    // A method calling a same-named function outside the class is not a cycle.
    "function run(): void {} class Job { run(): void { run(); } other(): void { this.missing(); } }",
    // Calls outside any named function, and anonymous functions.
    "setTimeout(() => { setTimeout(() => undefined, 1); }, 1); export default function (): void {}",
    // Calls through other objects and computed members are not followed.
    "class A { go(): void { this['go'](); } } const b = { go(): void { b.go(); } };",
    "const go = 'go'; class C { go(): void { this[go](); } }",
    // A diamond: c is reached twice from a without a cycle.
    "function c(): void {} function b(): void { c(); } function a(): void { b(); c(); }",
    // Class members that are not functions, and overload signatures.
    "class K { count = 0; ['computed'](): void {} }",
    "function f(x: number): void; function f(x: string): void; function f(x: unknown): void { void x; } f(1);",
  ],
  invalid: [
    {
      code: "function countdown(n: number): number { return n <= 0 ? 0 : countdown(n - 1); }",
      errors: [recursive("countdown")],
    },
    {
      code: "const isEven = (n: number): boolean => n === 0 || isOdd(n - 1);\nconst isOdd = (n: number): boolean => n !== 0 && isEven(n - 1);",
      errors: [recursive("isEven"), recursive("isOdd")],
    },
    {
      code: "class Tree { walk(): void { this.walk(); } visit = (): void => { this.visit(); }; }",
      errors: [recursive("walk"), recursive("visit")],
    },
    {
      // A call inside an anonymous callback counts for the named function around it.
      code: "function total(items: number[][]): number[] { return items.map((row) => total([row])[0] ?? 0); }",
      errors: [recursive("total")],
    },
    {
      code: "const walk = function (depth: number): void { if (depth > 0) { walk(depth - 1); } };",
      errors: [recursive("walk")],
    },
  ],
});

describe("plugin", () => {
  it("exposes the no-recursion rule", () => {
    expect(plugin.rules["no-recursion"]).toBe(noRecursion);
  });
});
