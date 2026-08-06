/**
 * @file GA grammar for tree-sitter
 * @author m410y
 * @license MIT
 */

// <reference types="tree-sitter-cli/dsl" />

// All visible names must be less then 15 chars long
// to *always* enable C++ std::string SSO 
export default grammar({
  name: "ga",

  inline: $ => [
    $.terminator,
  ],

  supertypes: $ => [
    $.expression,
  ],

  rules: {
    source_file: $ => $.expression,

    expression: $ => choice(
      $.identifier,
      $.binary_plus,
      $.binary_minus,
      $.geom_product,
    ),

    binary_plus: $ =>
      prec.left(20, seq($.expression, '+', $.expression)),
    binary_minus: $ =>
      prec.left(20, seq($.expression, '-', $.expression)),
    geom_product: $ =>
      prec.left(30, seq($.expression, $.expression)),

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
