" Pip syntax file
" Language:     Pip
" Maintainer:   Patrick Reynolds <reynolds@cs.duke.edu>
" Last change:  2005 Jul 29

if version < 600
	syn clear
elseif exists("b:current_syntax")
	finish
endif

syn keyword  pipType       validator invalidator fragment recognizer
syn keyword  pipStatement  level repeat message task thread
syn keyword  pipStatement  limit branch send recv
syn keyword  pipStatement  notice xor call between and maybe
syn keyword  pipStatement  assert instances unique in
syn keyword  pipStatement  during any average stddev
syn keyword  pipStatement  max min future done

syn region      pipCommentL     start="//" end="$" keepend
syn region			pipComment			start="/\*" end="\*/"
syn region			pipString       start=+L\="+ skip=+\\\\\|\\"+ end=+"+
syn region			pipRegex        start="m/" skip=+\\/+ end="/"
syn match       pipNumbers      display transparent "\<\d\|\.\d" contains=pipNumber
syn match       pipNumber       display contained "\d\+[a-z]*"

hi def link pipText Normal
hi def link pipType Type
hi def link pipStatement Statement
hi link pipString String
hi link pipRegex String
hi link pipNumber Number
hi def link pipCommentL pipComment
hi def link pipComment Comment

let b:current_syntax = "pip"
