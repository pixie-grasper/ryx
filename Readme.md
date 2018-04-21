# Truly RYX -- DO NOT confuse Pseudoryx!
## What is this?

Truly RYX is yet another parser generator.

## Why useful?

The LL(1) class has limited representability.
But if a grammar has completely been wrote in the LL(1) class,
that is easy to say the grammar is simple.

Truly RYX accepts a grammar if the grammar is in the LL(1).

## Get Started

```bash
$ git clone git://github.com/pixie-grasper/ryx.git
$ cd ryx
$ make
```

## Syntax

```
input = syntax*
      ;

syntax = ID '=' body_list ';'
       | '%' ID* ';'
       ;

body_list = body* ('|' body*)*
          ;

body = '(' body_list ')' body_opt*
     | ID body_opt*
     ;

body_opt = /[?+*]/
         | '{' NUM (',' NUM)? '}'
         ;
```

## TODO

- Generate codes
  - It has ability to syntax-check only now.

## Authors

- pixie-grasper (himajinn13sei@gmail.com)

## License

The GPL 3.0 and the later versions.
