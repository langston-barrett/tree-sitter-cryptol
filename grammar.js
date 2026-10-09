/**
 * @file Cryptol grammar for tree-sitter
 * @license MIT
 *
 * This grammar closely follows Cryptol's own Happy grammar
 * (src/Cryptol/Parser.y in GaloisInc/cryptol), so that it accepts the same
 * language.  Comments of the form `// Parser.y: foo` name the corresponding
 * Happy nonterminal.
 *
 * Layout (virtual braces and separators) is computed by the external scanner
 * in src/scanner.c, which mirrors src/Cryptol/Parser/Layout.hs.
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const sep1 = (rule, sep) => seq(rule, repeat(seq(sep, rule)));
const commaSep1 = (rule) => sep1(rule, ',');

const ID_FIRST = /[\p{L}_]/u;
const ID_NEXT = /[\p{L}\p{N}_']/u;
// Cryptol's lexer classifies non-ASCII symbols and punctuation as operator
// characters.
const OP_CHAR = choice(
  /[!#$%&*+\-./:<=>?@\\^|~]/,
  // No `u` flag: JavaScript does not support `&&`, but tree-sitter does.
  /[\p{S}\p{Pc}\p{Pd}\p{Po}&&[^\x00-\x7F]]/,
);

export default grammar({
  name: 'cryptol',

  externals: $ => [
    $._layout_start,
    $._layout_semicolon,
    $._layout_end,
    // Start of the implicit layout block of a file without a `module` header.
    $._module_start,
    '(', ')', '[', ']', '{', '}',
    $.comment,
    // Never produced; valid only during error recovery.
    $._error_sentinel,
    // Never valid; produced for tokens that Cryptol rejects because of their
    // indentation.
    $._invalid_indentation,
  ],

  extras: $ => [/\s/, $.comment],

  word: $ => $.identifier,

  conflicts: $ => [
  ],

  rules: {
    // Parser.y: top_module
    source_file: $ => choice(
      $.module,
      $.interface_module,
      seq($._module_start, optional($._top_declarations), $._layout_end),
    ),

    module: $ => seq('module', $._module_definition),

    // Parser.y: module_def
    _module_definition: $ => seq(
      field('name', $.module_name),
      choice(
        seq('where', field('body', $.declarations)),
        seq(
          '=',
          field('functor', $._import_name),
          optional(choice(
            seq('where', field('body', $.declarations)),
            field('arguments', $.module_arguments),
          )),
        ),
      ),
    ),

    declarations: $ => seq(
      $._layout_start,
      optional($._top_declarations),
      $._layout_end,
    ),

    interface_module: $ => seq(
      'interface',
      'module',
      field('name', $.module_name),
      $._interface_rhs,
    ),

    _interface_rhs: $ => choice(
      seq('where', field('body', $.signature)),
      seq(
        '=',
        field('functor', $._import_name),
        choice(
          seq('where', field('body', $.declarations)),
          field('arguments', $.module_arguments),
        ),
      ),
    ),

    // Parser.y: sig_body
    signature: $ => seq(
      $._layout_start,
      optional(sep1(
        choice($.import, $._parameter_declaration),
        $._semicolon,
      )),
      $._layout_end,
    ),

    // Parser.y: modInstParams
    module_arguments: $ => seq(
      '{',
      choice(
        $._module_argument,
        commaSep1($.named_module_argument),
      ),
      '}',
    ),

    named_module_argument: $ => seq(
      field('name', $._ident),
      '=',
      field('value', $._module_argument),
    ),

    // Parser.y: modInstParam
    _module_argument: $ => choice(
      $._import_name,
      $.interface_argument,
      $.wildcard,
    ),

    interface_argument: $ => seq('interface', $._ident),

    // Repeated layout separators only arise around documentation comments
    // (see src/scanner.c).
    _semicolon: $ => choice(repeat1($._layout_semicolon), ';'),

    _top_declarations: $ => sep1($._top_declaration, $._semicolon),

    // Parser.y: vtop_decl
    _top_declaration: $ => choice(
      $._declaration,
      $.include,
      $.property,
      $.newtype,
      $.enum,
      $.primitive,
      $.primitive_type,
      $.foreign,
      $.private,
      $.interface_constraint,
      $.parameter,
      $.submodule,
      $.interface_submodule,
      $.import,
    ),

    include: $ => seq('include', field('path', $.string)),

    property: $ => seq(
      'property',
      field('name', $._ident),
      repeat(field('parameter', $._apat)),
      '=',
      field('body', $._expr),
    ),

    private: $ => seq(
      'private',
      $._layout_start,
      $._top_declarations,
      $._layout_end,
    ),

    // Parser.y: prim_bind
    primitive: $ => seq(
      'primitive',
      field('name', $._var),
      ':',
      field('type', $.schema),
    ),

    primitive_type: $ => seq(
      'primitive',
      'type',
      field('type', $.schema),
      ':',
      field('kind', $._kind),
    ),

    // Parser.y: foreign_bind
    foreign: $ => seq(
      'foreign',
      optional(field('convention', $._ident)),
      field('name', $._ident),
      ':',
      field('type', $.schema),
    ),

    interface_constraint: $ => seq('interface', 'constraint', $._type),

    submodule: $ => seq('submodule', $._module_definition),

    interface_submodule: $ => seq(
      'interface',
      'submodule',
      field('name', $._ident),
      choice(
        seq('where', field('body', $.signature)),
        seq(
          '=',
          field('functor', $._import_name),
          optional(choice(
            seq('where', field('body', $.declarations)),
            field('arguments', $.module_arguments),
          )),
        ),
      ),
    ),

    // Parser.y: parameter_decls
    parameter: $ => seq(
      'parameter',
      $._layout_start,
      sep1($._parameter_declaration, $._semicolon),
      $._layout_end,
    ),

    // Parser.y: par_decl
    _parameter_declaration: $ => choice(
      $.signature_declaration,
      $.type_parameter,
      $.type_synonym,
      $.constraint_synonym,
      $.parameter_constraint,
    ),

    type_parameter: $ => seq(
      'type',
      commaSep1(field('name', $._type)),
      ':',
      field('kind', $._kind),
    ),

    parameter_constraint: $ => seq('type', 'constraint', $._type),

    // Parser.y: import, iface_import
    import: $ => seq(
      'import',
      choice(
        seq(
          optional(field('interface', 'interface')),
          field('module', $._import_name),
          optional(field('arguments', $.module_arguments)),
          optional($._import_as),
          optional(field('list', $.import_list)),
          optional(seq('where', field('declarations', $.local_declarations))),
        ),
        seq(
          field('module', $._import_name_backtick),
          optional($._import_as),
          optional(field('list', $.import_list)),
        ),
      ),
    ),

    _import_as: $ => seq('as', field('alias', $.module_name)),

    import_list: $ => seq(
      optional('hiding'),
      '(',
      commaSep1($._var),
      ')',
    ),

    // Parser.y: impName
    _import_name: $ => choice(
      $.module_name,
      $.submodule_name,
    ),

    submodule_name: $ => seq('submodule', $._qname),

    // Parser.y: impNameBT
    _import_name_backtick: $ => choice(
      alias($._submodule_name_backtick, $.submodule_name),
      seq('`', $.module_name),
    ),

    _submodule_name_backtick: $ => seq('submodule', '`', $._qname),

    // Parser.y: modName
    module_name: $ => seq(
      optional('module'),
      choice($._ident, $.qualified_name),
    ),

    // ------------------------------------------------------------------------
    // Declarations

    // Parser.y: decl
    _declaration: $ => choice(
      $.signature_declaration,
      $.pattern_binding,
      $.function,
      $.infix_function,
      $.type_synonym,
      $.constraint_synonym,
      $.fixity,
    ),

    signature_declaration: $ => seq(
      commaSep1(field('name', $._var)),
      ':',
      field('type', $.schema),
    ),

    // A binding whose left-hand side is not a single name.
    pattern_binding: $ => seq(
      field('pattern', $._binding_pattern),
      '=',
      field('body', $._expr),
    ),

    _binding_pattern: $ => choice(
      alias($._ipat_split, $.split_pattern),
      $._apat_not_var,
    ),

    function: $ => seq(
      field('name', $._var),
      repeat(field('parameter', $._apat)),
      repeat(seq('@', field('index', $._apat))),
      choice(
        seq('=', field('body', $._expr)),
        repeat1($.guard),
      ),
    ),

    // Parser.y: propguards_case
    guard: $ => seq(
      '|',
      field('constraint', $._type),
      '=>',
      field('body', $._expr),
    ),

    infix_function: $ => seq(
      field('left', $._apat),
      field('operator', $._pat_op),
      field('right', $._apat),
      '=',
      field('body', $._expr),
    ),

    type_synonym: $ => seq(
      'type',
      field('name', $._type),
      '=',
      field('value', $._type),
    ),

    constraint_synonym: $ => seq(
      'type',
      'constraint',
      field('name', $._type),
      '=',
      field('value', $._type),
    ),

    fixity: $ => seq(
      choice('infixl', 'infixr', 'infix'),
      field('precedence', $.number),
      commaSep1(field('operator', $._op)),
    ),

    newtype: $ => seq(
      'newtype',
      field('name', $._type),
      '=',
      field('body', $.record_type),
      optional($.deriving),
    ),

    enum: $ => seq(
      'enum',
      field('name', $._type),
      '=',
      optional('|'),
      sep1($.constructor, '|'),
      optional($.deriving),
    ),

    // Parser.y: enum_con
    constructor: $ => $._app_type,

    deriving: $ => seq('deriving', '(', commaSep1($._var), ')'),

    // Parser.y: whereClause
    local_declarations: $ => seq(
      $._layout_start,
      optional(sep1($._declaration, $._semicolon)),
      $._layout_end,
    ),

    // ------------------------------------------------------------------------
    // Names and operators

    // Parser.y: ident.  Some keywords are also allowed as identifiers.
    _ident: $ => choice(
      $.identifier,
      alias(choice('x', 'private', 'as', 'hiding'), $.identifier),
    ),

    // Parser.y: qname
    _qname: $ => choice($._ident, $.qualified_name),

    // Parser.y: var
    _var: $ => choice($._ident, $.parenthesized_operator),

    parenthesized_operator: $ => seq('(', $._op, ')'),

    // Parser.y: pat_op
    _pat_op: $ => choice(
      $.operator,
      alias(choice('*', '+', '-', '~', '^^', '<', '>'), $.operator),
    ),

    // Parser.y: op
    _op: $ => choice(
      $._pat_op,
      alias(choice('#', '@'), $.operator),
    ),

    // Parser.y: qop
    _qop: $ => choice($._op, $.qualified_operator),

    // ------------------------------------------------------------------------
    // Expressions

    // Parser.y: expr
    _expr: $ => choice($._expr_no_where, $.where_expression),

    where_expression: $ => seq(
      field('body', $._expr),
      'where',
      field('declarations', $.local_declarations),
    ),

    // Parser.y: exprNoWhere
    _expr_no_where: $ => choice(
      alias($._long_binary_expression, $.binary_expression),
      $._long_rhs,
      $.typed_expression,
    ),

    _long_binary_expression: $ => seq(
      field('left', $._simple_expr),
      field('operator', $._qop),
      field('right', $._long_rhs),
    ),

    typed_expression: $ => seq(
      field('expression', $._simple_expr),
      ':',
      field('type', $._type),
    ),

    // Parser.y: simpleExpr
    _simple_expr: $ => choice($.binary_expression, $._simple_rhs),

    binary_expression: $ => seq(
      field('left', $._simple_expr),
      field('operator', $._qop),
      field('right', $._simple_rhs),
    ),

    // Parser.y: longExpr
    _long_expr: $ => choice(
      $.if_expression,
      $.lambda,
      $.case_expression,
    ),

    if_expression: $ => seq(
      'if',
      sep1(seq(
        field('condition', $._expr),
        'then',
        field('consequence', $._expr),
      ), '|'),
      'else',
      field('alternative', $._expr_no_where),
    ),

    lambda: $ => seq(
      '\\',
      repeat1(field('parameter', $._apat)),
      '->',
      field('body', $._expr_no_where),
    ),

    case_expression: $ => seq(
      'case',
      field('scrutinee', $._expr),
      'of',
      $._layout_start,
      sep1($.case_alternative, $._semicolon),
      $._layout_end,
    ),

    case_alternative: $ => seq(
      field('pattern', $._cpat),
      '->',
      field('body', $._expr),
    ),

    // Parser.y: simpleRHS
    _simple_rhs: $ => choice(
      alias($._simple_prefix_expression, $.prefix_expression),
      $._simple_app,
    ),

    _simple_prefix_expression: $ => seq(
      field('operator', alias(choice('-', '~'), $.operator)),
      field('operand', $._simple_app),
    ),

    // Parser.y: longRHS
    _long_rhs: $ => choice(
      alias($._long_prefix_expression, $.prefix_expression),
      $._long_app,
    ),

    _long_prefix_expression: $ => seq(
      field('operator', alias(choice('-', '~'), $.operator)),
      field('operand', $._long_app),
    ),

    // Parser.y: simpleApp
    _simple_app: $ => choice($.application, $._aexpr),

    application: $ => seq(
      field('function', $._aexpr),
      repeat1(field('argument', $._aexpr)),
    ),

    // Parser.y: longApp
    _long_app: $ => choice(
      alias($._long_application, $.application),
      $._long_expr,
      $._simple_app,
    ),

    _long_application: $ => seq(
      field('function', $._aexpr),
      repeat(field('argument', $._aexpr)),
      field('argument', $._long_expr),
    ),

    // Parser.y: aexpr
    _aexpr: $ => choice($._no_sel_aexpr, $.selector_expression),

    selector_expression: $ => seq(
      field('value', $._aexpr),
      field('selector', $.selector),
    ),

    // Parser.y: no_sel_aexpr
    _no_sel_aexpr: $ => choice(
      $._qname,
      $.number,
      $.string,
      $.character,
      $.wildcard,
      $.parenthesized_expression,
      $.tuple,
      $.record,
      $.record_update,
      $.list,
      $.list_comprehension,
      $.range,
      $.type_argument,
      $.operator_section,
      $.polynomial,
    ),

    parenthesized_expression: $ => seq('(', $._expr, ')'),

    tuple: $ => seq(
      '(',
      optional(seq($._expr, repeat1(seq(',', $._expr)))),
      ')',
    ),

    record: $ => seq(
      '{',
      optional(commaSep1($.field_assignment)),
      '}',
    ),

    record_update: $ => seq(
      '{',
      field('record', $._expr),
      '|',
      commaSep1($.field_assignment),
      '}',
    ),

    // Parser.y: field_expr
    field_assignment: $ => seq(
      field('field', $._simple_expr),
      choice('=', '->'),
      field('value', $._expr),
    ),

    list: $ => seq(
      '[',
      optional(commaSep1($._expr)),
      ']',
    ),

    list_comprehension: $ => seq(
      '[',
      field('body', $._expr),
      repeat1(seq('|', $.generators)),
      ']',
    ),

    // One arm of a (possibly parallel) comprehension.
    // Parser.y: matches
    generators: $ => commaSep1($.generator),

    // Parser.y: match
    generator: $ => seq(
      field('pattern', $._itpat),
      '<-',
      field('source', $._expr),
    ),

    // Parser.y: list_expr (the enumeration forms)
    range: $ => seq(
      '[',
      field('from', $._expr),
      choice(
        seq(',', field('then', $._expr), '..', field('to', $._expr)),
        seq(
          '..',
          field('to', $._expr),
          optional(choice(
            seq('by', field('step', $._expr)),
            seq('down', 'by', field('step', $._expr)),
          )),
        ),
        seq(
          choice(seq('..', '<'), '..<'),
          field('to', $._expr),
          optional(seq('by', field('step', $._expr))),
        ),
        seq(
          choice(seq('..', '>'), '..>'),
          field('to', $._expr),
          'down',
          'by',
          field('step', $._expr),
        ),
        seq(optional(seq(',', field('then', $._expr))), '...'),
      ),
      ']',
    ),

    // Parser.y: '`' tick_ty
    type_argument: $ => seq(
      '`',
      choice(
        $._qname,
        $.number,
        seq('(', $._type, ')'),
        seq('{', optional(choice(
          commaSep1($.type_field_assignment),
          commaSep1($._type),
        )), '}'),
      ),
    ),

    // Parser.y: field_ty_val
    type_field_assignment: $ => seq(
      field('name', $._ident),
      '=',
      field('value', $._type),
    ),

    operator_section: $ => seq('(', $._qop, ')'),

    polynomial: $ => seq(
      '<|',
      optional(sep1($.polynomial_term, '+')),
      '|>',
    ),

    polynomial_term: $ => choice(
      $.number,
      seq('x', optional(seq('^^', $.number))),
    ),

    // ------------------------------------------------------------------------
    // Patterns

    // Parser.y: pat
    _pat: $ => choice(
      alias($._cpat_typed, $.typed_pattern),
      $._cpat,
    ),

    _cpat_typed: $ => seq(
      field('pattern', $._cpat),
      ':',
      field('type', $._type),
    ),

    // Parser.y: cpat
    _cpat: $ => choice(
      alias($._cpat_split, $.split_pattern),
      $.constructor_pattern,
      $._apat,
    ),

    _cpat_split: $ => prec.right(seq(
      field('left', $._cpat),
      '#',
      field('right', $._cpat),
    )),

    constructor_pattern: $ => seq(
      field('constructor', $._qname),
      repeat1(field('argument', $._apat)),
    ),

    // Parser.y: apat
    _apat: $ => choice(
      $._qname,
      $._apat_not_var,
    ),

    _apat_not_var: $ => choice(
      $.wildcard,
      alias($._parenthesized_pattern, $.parenthesized_pattern),
      alias($._tuple_pattern, $.tuple_pattern),
      alias($._list_pattern, $.list_pattern),
      alias($._record_pattern, $.record_pattern),
    ),

    _parenthesized_pattern: $ => seq('(', $._pat, ')'),

    _tuple_pattern: $ => seq(
      '(',
      optional(seq($._pat, repeat1(seq(',', $._pat)))),
      ')',
    ),

    _list_pattern: $ => seq('[', optional(commaSep1($._pat)), ']'),

    _record_pattern: $ => seq(
      '{',
      optional(commaSep1($.field_pattern)),
      '}',
    ),

    field_pattern: $ => seq(
      field('name', $._ident),
      '=',
      field('pattern', $._pat),
    ),

    // Parser.y: itpat
    _itpat: $ => choice(
      alias($._ipat_typed, $.typed_pattern),
      $._ipat,
    ),

    _ipat_typed: $ => seq(
      field('pattern', $._ipat),
      ':',
      field('type', $._type),
    ),

    // Parser.y: ipat
    _ipat: $ => choice(
      alias($._ipat_split, $.split_pattern),
      $._apat,
    ),

    _ipat_split: $ => prec.right(seq(
      field('left', $._ipat),
      '#',
      field('right', $._ipat),
    )),

    // ------------------------------------------------------------------------
    // Types

    // Parser.y: schema
    schema: $ => seq(
      optional(field('parameters', $.type_parameters)),
      repeat(seq(field('constraint', $._type), '=>')),
      field('type', $._type),
    ),

    // Parser.y: schema_vars
    type_parameters: $ => seq(
      '{',
      optional(commaSep1($.type_parameter_declaration)),
      '}',
    ),

    // Parser.y: schema_param
    type_parameter_declaration: $ => seq(
      field('name', $._ident),
      optional(seq(':', field('kind', $._kind))),
    ),

    // Parser.y: kind
    _kind: $ => choice(
      alias(choice('#', '*', 'Prop'), $.kind),
      $.function_kind,
    ),

    function_kind: $ => prec.right(seq(
      field('parameter', $._kind),
      '->',
      field('result', $._kind),
    )),

    // Parser.y: type
    _type: $ => choice($.function_type, $._infix_type),

    function_type: $ => seq(
      field('parameter', $._infix_type),
      '->',
      field('result', $._type),
    ),

    // Parser.y: infix_type
    _infix_type: $ => choice($.infix_type, $._app_type),

    infix_type: $ => seq(
      field('left', $._infix_type),
      field('operator', $._op),
      field('right', $._app_type),
    ),

    // Parser.y: app_type
    _app_type: $ => choice(
      $.sequence_type,
      $.type_application,
      $._atype,
    ),

    sequence_type: $ => seq(
      repeat1(seq('[', field('length', $._type), ']')),
      field('element', $._atype),
    ),

    type_application: $ => seq(
      field('constructor', $._qname),
      repeat1(field('argument', $._atype)),
    ),

    // Parser.y: atype
    _atype: $ => choice(
      $._qname,
      $.parenthesized_operator_type,
      $.number,
      $.character,
      alias($._bit_sequence_type, $.sequence_type),
      $.parenthesized_type,
      $.tuple_type,
      $.record_type,
      $.wildcard,
    ),

    parenthesized_operator_type: $ => seq('(', $._qop, ')'),

    _bit_sequence_type: $ => seq('[', field('length', $._type), ']'),

    // Parser.y: '(' ktype ')'
    parenthesized_type: $ => seq(
      '(',
      field('type', $._type),
      optional(seq(':', field('kind', $._kind))),
      ')',
    ),

    tuple_type: $ => seq(
      '(',
      optional(seq($._type, repeat1(seq(',', $._type)))),
      ')',
    ),

    record_type: $ => seq(
      '{',
      optional(commaSep1($.field_type)),
      '}',
    ),

    field_type: $ => seq(
      field('name', $._ident),
      ':',
      field('type', $._type),
    ),

    // ------------------------------------------------------------------------
    // Tokens

    identifier: _ => token(seq(ID_FIRST, repeat(ID_NEXT))),

    qualified_name: _ => token(seq(
      repeat1(seq(ID_FIRST, repeat(ID_NEXT), '::')),
      ID_FIRST, repeat(ID_NEXT),
    )),

    operator: _ => token(repeat1(OP_CHAR)),

    qualified_operator: _ => token(seq(
      repeat1(seq(ID_FIRST, repeat(ID_NEXT), '::')),
      repeat1(OP_CHAR),
    )),

    selector: _ => token(seq('.', repeat1(ID_NEXT))),

    number: _ => token(seq(
      /[0-9]/,
      repeat(ID_NEXT),
      optional(seq('.', repeat1(choice(ID_NEXT, /[pPeE][+-]/)))),
    )),

    string: _ => token(seq('"', repeat(choice(/[^"\\\n]/, /\\./)), '"')),

    character: _ => token(seq('\'', repeat1(choice(/[^'\\\n]/, /\\./)), '\'')),

    wildcard: _ => '_',
  },
});
