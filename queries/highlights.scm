; Keywords

[
  "module"
  "submodule"
  "interface"
  "import"
  "as"
  "hiding"
  "where"
  "private"
  "parameter"
  "type"
  "constraint"
  "newtype"
  "enum"
  "deriving"
  "property"
  "primitive"
  "foreign"
  "include"
  "infix"
  "infixl"
  "infixr"
] @keyword

[
  "if"
  "then"
  "else"
  "case"
  "of"
] @keyword.conditional

[
  "by"
  "down"
] @keyword

; Literals

(number) @number
(string) @string
(character) @character
(comment) @comment

((comment) @comment.documentation
  (#match? @comment.documentation "^/[*][*][^*/]"))

; Types

(kind) @type.builtin

(type_application
  constructor: (identifier) @type)
(type_application
  constructor: (qualified_name) @type)
(type_synonym
  name: (identifier) @type.definition)
(type_synonym
  name: (type_application
    constructor: (identifier) @type.definition))
(constraint_synonym
  name: (type_application
    constructor: (identifier) @type.definition))
(newtype
  name: (identifier) @type.definition)
(newtype
  name: (type_application
    constructor: (identifier) @type.definition))
(enum
  name: (identifier) @type.definition)
(enum
  name: (type_application
    constructor: (identifier) @type.definition))
(type_parameter_declaration
  name: (identifier) @type.parameter)
(field_type
  name: (identifier) @property)

; Constructors

(constructor
  (identifier) @constructor)
(constructor
  (type_application
    constructor: (identifier) @constructor))
(constructor_pattern
  constructor: (identifier) @constructor)

; Modules

(module_name) @module
(submodule_name) @module

; Functions and variables

(signature_declaration
  name: (identifier) @function)
(function
  name: (identifier) @function)
(infix_function
  operator: (operator) @function)
(property
  name: (identifier) @function)
(primitive
  name: (identifier) @function)
(foreign
  name: (identifier) @function)
(application
  function: (identifier) @function.call)

(function
  parameter: (identifier) @variable.parameter)
(lambda
  parameter: (identifier) @variable.parameter)

(selector) @property
(field_assignment
  field: (identifier) @property)
(field_pattern
  name: (identifier) @property)
(type_field_assignment
  name: (identifier) @property)

; Operators and punctuation

[
  (operator)
  (qualified_operator)
  "="
  "->"
  "=>"
  "<-"
  "\\"
  "|"
  ":"
  "@"
  "#"
  ".."
  "..<"
  "..>"
  "..."
  "<|"
  "|>"
] @operator

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  ","
  ";"
  "`"
] @punctuation.delimiter

(wildcard) @variable.builtin
