/**
 * @file GA grammar for tree-sitter
 * @author m410y
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

export default grammar({
  name: "ga",

  inline: $ => [
    $.terminator,
  ],

  supertypes: $ => [
    $.top_level_statement,
    $.expression,
    $.type,
    $.literal,
    $.binary_expression,
    $.number_literal,
  ],

  rules: {
    source_file: $ => repeat($.top_level_statement),

    // top-level statements

    top_level_statement: $ => choice(
      $.using_statement,
      $.function_definition,
    ),

    using_statement: $ => seq(
      "using",
      choice(
        $.simple_metric,
        $.compact_metric,
        $.general_metric,
      ),
      $.terminator
    ),

    simple_metric: _ => /[\+\-0]+/,
    compact_metric: $ => seq('<', sep1($.int_literal), '>'),
    general_metric: $ => seq('{', sep1($.number_literal), '}'),

    function_definition: $ => seq(
      "function",
      field("name", $.identifier),
      field("params", $.parameter_list),
      "->",
      field("type", $.type),
      field("body", $.block),
    ),

    parameter_list: $ => seq('(', sep($.variable_declaration), ')'),

    block: $ => seq(
      sep($.terminator, choice(
        $.expression,
        $.variable_declaration,
        $.variable_definition,
        $.return_statement,
      )),
      "end"
    ),

    // statements

    variable_definition: $ => seq(
      field("decl", $.variable_declaration),
      '=',
      field("expr", $.expression),
    ),

    variable_declaration: $ => seq(
      field("type", $.type),
      field("name", $.identifier),
    ),

    return_statement: $ => seq("return", $.expression),

    // expressions

    expression: $ => choice(
      $.literal,
      $.identifier,
      $.parenthesized_expression,
      $.call_expression,
      $.unary_plus,
      $.unary_minus,
      $.projection,
      $.binary_expression,
    ),

    call_expression: $ => prec(50, seq(
      field("name", $.identifier),
      field("args", $.argument_list),
    )),

    argument_list: $ => seq(token.immediate('('), sep($.expression), ')'),

    parenthesized_expression: $ => seq('(', $.expression, ')'),

    projection: $ => prec.right(
      seq('<', $.expression, '>', optional(field("type", $.type)))
    ),

    // operators

    unary_plus: $ =>
      prec.left(15, seq('+', $.expression)),
    unary_minus: $ =>
      prec.left(15, seq('-', $.expression)),

    binary_expression: $ => choice(
      $.assignment,
      $.binary_plus,
      $.binary_minus,
      $.geometric_product,
      $.dot_product,
      $.wedge_product,
      $.vee_product,
      $.scalar_product,
    ),

    assignment: $ =>
      prec.right(5, seq($.expression, '=', $.expression)),
    binary_plus: $ =>
      prec.left(20, seq($.expression, '+', $.expression)),
    binary_minus: $ =>
      prec.left(20, seq($.expression, '-', $.expression)),
    geometric_product: $ =>
      prec.left(30, seq($.expression, $.expression)),
    dot_product: $ =>
      prec.left(35, seq($.expression, '.', $.expression)),
    wedge_product: $ =>
      prec.left(40, seq($.expression, 'w', $.expression)),
    vee_product: $ =>
      prec.left(40, seq($.expression, 'v', $.expression)),
    scalar_product: $ =>
      prec.left(45, seq($.expression, '*', $.expression)),

    // types

    type: $ => prec(60, choice(
      $.identifier,
      $.union_type,
    )),

    union_type: $ => seq('{', sep(choice($.int_literal, $.type)), '}'),

    // literals

    literal: $ => choice(
      $.number_literal,
      $.basis_literal,
    ),

    number_literal: $ => choice(
      $.int_literal,
      $.float_literal,
    ),

    int_literal: _ => /\d+/,

    float_literal: _ => choice(
      /\d+\.\d*/,
      /\d*\.\d+/,
    ),

    basis_literal: _ => /e\d+/,

    identifier: _ => choice(
      /[\w&&[^wveI]]/,
      /[\w&&[^\d]]\w+/
    ),

    terminator: _ => choice(/\r?\n/, ';'),
  },

});

/**
 * Creates a rule to optionally match one or more of the rules separated by a separator
 *
 * @param {Rule} rule
 * @param {RuleOrLiteral} separator
 *
 * @returns {ChoiceRule}
 */
function sep(rule, separator = ',') {
  return optional(sep1(rule, separator));
}

/**
 * Creates a rule to match one or more of the rules separated by a separator
 *
 * @param {Rule} rule
 * @param {RuleOrLiteral} separator
 *
 * @returns {SeqRule}
 */
function sep1(rule, separator = ',') {
  return seq(rule, repeat(seq(separator, rule)));
}
