// ics/no-recursion (ICS-007): rejects functions that call themselves, directly
// or through other functions in the same file. The TypeScript counterpart of
// python/ics_lint's ICS101 and clang-tidy's misc-no-recursion.
//
// A call is resolved when its callee is a plain name bound to a function in
// this file (found through the scope, so shadowing is respected) or
// this.method() on the enclosing class. Calls inside anonymous callbacks count
// for the nearest named function around them. Recursion across files or
// through other objects is not detected.
import { AST_NODE_TYPES, ASTUtils, ESLintUtils, TSESLint, type TSESTree } from "@typescript-eslint/utils";

type FunctionNode = TSESTree.ArrowFunctionExpression | TSESTree.FunctionDeclaration | TSESTree.FunctionExpression;
type CallGraph = ReadonlyMap<FunctionNode, ReadonlySet<FunctionNode>>;
type Context = Readonly<TSESLint.RuleContext<"recursive", []>>;

const createRule = ESLintUtils.RuleCreator(
  (name) => `https://github.com/MatthewK84/ICS/blob/main/web/packages/eslint-plugin-ics/src/${name}.ts`,
);

function isFunction(node: TSESTree.Node | null | undefined): node is FunctionNode {
  return (
    node?.type === AST_NODE_TYPES.ArrowFunctionExpression ||
    node?.type === AST_NODE_TYPES.FunctionDeclaration ||
    node?.type === AST_NODE_TYPES.FunctionExpression
  );
}

// The innermost named function that contains the node, if any.
function owningFunction(
  ancestors: readonly TSESTree.Node[],
  named: ReadonlyMap<FunctionNode, string>,
): FunctionNode | undefined {
  return ancestors.findLast((node): node is FunctionNode => isFunction(node) && named.has(node));
}

// The function a class member of the given name holds, in the class around the call.
function classMember(ancestors: readonly TSESTree.Node[], name: string): FunctionNode | undefined {
  const body = ancestors.findLast((node): node is TSESTree.ClassBody => node.type === AST_NODE_TYPES.ClassBody);
  const member = body?.body.find(
    (element): element is TSESTree.MethodDefinition | TSESTree.PropertyDefinition =>
      (element.type === AST_NODE_TYPES.MethodDefinition || element.type === AST_NODE_TYPES.PropertyDefinition) &&
      element.key.type === AST_NODE_TYPES.Identifier &&
      element.key.name === name,
  );
  return member !== undefined && isFunction(member.value) ? member.value : undefined;
}

// The function a name refers to at the call, found through the scope. A
// parameter's definition node is its function, so the kind of definition is
// checked, not only the node.
function resolveName(context: Context, call: TSESTree.CallExpression, name: string): FunctionNode | undefined {
  const variable = ASTUtils.findVariable(context.sourceCode.getScope(call), name);
  const definition = variable?.defs[0];
  if (definition?.type === TSESLint.Scope.DefinitionType.FunctionName && isFunction(definition.node)) {
    return definition.node;
  }
  if (definition?.type === TSESLint.Scope.DefinitionType.Variable && isFunction(definition.node.init)) {
    return definition.node.init;
  }
  return undefined;
}

function resolveCallee(context: Context, call: TSESTree.CallExpression): FunctionNode | undefined {
  const { callee } = call;
  if (callee.type === AST_NODE_TYPES.Identifier) {
    return resolveName(context, call, callee.name);
  }
  if (
    callee.type === AST_NODE_TYPES.MemberExpression &&
    !callee.computed &&
    callee.object.type === AST_NODE_TYPES.ThisExpression &&
    callee.property.type === AST_NODE_TYPES.Identifier
  ) {
    return classMember(context.sourceCode.getAncestors(call), callee.property.name);
  }
  return undefined;
}

function buildGraph(
  context: Context,
  named: ReadonlyMap<FunctionNode, string>,
  calls: readonly TSESTree.CallExpression[],
): CallGraph {
  const graph = new Map<FunctionNode, Set<FunctionNode>>();
  for (const call of calls) {
    const caller = owningFunction(context.sourceCode.getAncestors(call), named);
    const callee = resolveCallee(context, call);
    if (caller !== undefined && callee !== undefined) {
      graph.set(caller, new Set([...(graph.get(caller) ?? []), callee]));
    }
  }
  return graph;
}

// Depth-first search with an explicit stack, so the rule never recurses itself.
export function reachesItself(graph: CallGraph, start: FunctionNode): boolean {
  const seen = new Set<FunctionNode>();
  const stack = [...(graph.get(start) ?? [])];
  for (let current = stack.pop(); current !== undefined; current = stack.pop()) {
    if (current === start) {
      return true;
    }
    if (!seen.has(current)) {
      seen.add(current);
      stack.push(...(graph.get(current) ?? []));
    }
  }
  return false;
}

export const noRecursion = createRule({
  name: "no-recursion",
  meta: {
    type: "problem",
    docs: { description: "Disallow functions that call themselves, directly or through other functions in the file" },
    messages: { recursive: "Function '{{name}}' is recursive; rewrite it with a loop (ICS-007)." },
    schema: [],
  },
  defaultOptions: [],
  create(context) {
    const named = new Map<FunctionNode, string>();
    const calls: TSESTree.CallExpression[] = [];
    const nameMember = (node: TSESTree.MethodDefinition | TSESTree.PropertyDefinition): void => {
      if (node.key.type === AST_NODE_TYPES.Identifier && isFunction(node.value)) {
        named.set(node.value, node.key.name);
      }
    };
    return {
      FunctionDeclaration(node): void {
        if (node.id !== null) {
          named.set(node, node.id.name);
        }
      },
      VariableDeclarator(node): void {
        if (node.id.type === AST_NODE_TYPES.Identifier && isFunction(node.init)) {
          named.set(node.init, node.id.name);
        }
      },
      MethodDefinition: nameMember,
      PropertyDefinition: nameMember,
      CallExpression(node): void {
        calls.push(node);
      },
      "Program:exit"(): void {
        const graph = buildGraph(context, named, calls);
        for (const [node, name] of named) {
          if (reachesItself(graph, node)) {
            context.report({ node, messageId: "recursive", data: { name } });
          }
        }
      },
    };
  },
});
