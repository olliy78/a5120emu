	;DISP.INC
	;Prerequisite	n/a

dispc:	push 	af
      	push 	hl
      	push 	de
      	push 	bc
      	ld   	e,a
      	ld   	c,2
      	call 	5
      	pop  	bc
      	pop  	de
      	pop  	hl   
      	pop  	af
      	ret

dispw:	call	dispwi
	push	af
	ld	a,20h
	call	dispc
	pop	af
	ret

dispwi:	push	af
	ld	a,h
	call	dispbi
	ld	a,l
	call	dispbi
	pop	af
	ret

dispb:	call	dispbi
	push	af
	ld	a,20h
	call	dispc
	pop	af
	ret

dispbi:	push 	af		;display byte intern
      	srl  	a
      	srl  	a
      	srl  	a
      	srl  	a
      	call 	outn
      	pop  	af
      	push 	af
      	and  	0fh
      	call 	outn
      	pop  	af
      	ret  	

outn: 	cp   	0ah
      	jr   	c,num
      	add  	a,37h
      	jr   	outp
num:  	add  	a,30h
outp: 	call	dispc
	ret

disps:	push	hl
	push	de
	push	bc
	push	af
	ld	c,09
	call	5
	pop	af
	pop	bc
	pop	de
	pop	hl
	ret

dispa: 	push	af
	call	dispw
	ld	e,':'
	call	dispc
	ld	a,(hl)
	call	dispb	
	pop	af	
	ret

;----------------------------------------
;Subroutine f. display memory hex / ascii
	;hl = startadr
	;de = anzeigeadr
	;(memrow) = rowcount
	;(memcol) = byte pro zeile
;----------------------------------------
dispm:	ld	a,(memrow)
	ld	b,a

nxln:	push	bc

	push	de
	ld	de,txcrlf	;Zeilenumbruch
	call	disps
	pop	de

	ex	de,hl
	call	dispwi		;Adresse
	ex	de,hl

	ld	a,(memcol)	;Anzahl der Spalten als Laufvariable
	ld	b,a

	push	de
	ld	de,txdpsp	; ': '
	call	disps
	pop	de
	
	push	hl
nxhl:	ld	a,(hl)

	call	dispbi		;Ausgabe des Byte 
	ld	a,20h
	call	dispc

	inc	hl
	inc	de
	
	djnz	nxhl	
	pop	hl
	
	ld	a,(memcol)
	ld	b,a		;next ascii
nxhla:	ld	a,(hl)
	res	7,a		
	cp	20h		;characters 00-1f und 80-9f als "dot"
	ld	a,(hl)		;a richtig laden, carry bleibt erhalten
	jr	nc,normc	;wenn carry, das "dot" laden
	ld	a,'.'
normc:	call	dispc
	inc	hl
	djnz	nxhla
		
	pop	bc
	djnz	nxln

	ret

waitk:	push	af
	push	hl
	push	de
	push	bc
	ld	c,01
	call	0005
	pop	bc
	pop	de
	pop	hl
	pop	af
	ret

;---------------------------------------------------------------------------------
;subroutine disphl
;Anzeige Register HL
disphl:	push	de
	ld	de,txhl
	call	disps
	pop	de
	call	dispw
	ret
;---------------------------------------------------------------------------------
;subroutine dispde
;Anzeige Register DE
dispde:	push	hl
	push	de
	ld	de,txde
	call	disps
	pop	de
	ld	h,d
	ld	l,e
	call	dispw
	pop	hl
	ret
;---------------------------------------------------------------------------------
;subroutine dispbc
;Anzeige Register BC
dispbc:	push	hl
	push	de
	ld	de,txbc
	call	disps
	pop	de
	ld	h,b
	ld	l,c
	call	dispw
	pop	hl
	ret
;---------------------------------------------------------------------------------
;subroutine dispra
;Anzeige Register A
dispra:	push	hl
	push	de
	ld	de,txra
	call	disps
	call	dispb
	pop	de
	pop	hl
	ret
;---------------------------------------------------------------------------------
;subroutine debug
;einfache Debugausgabe auf Konsole
dbg:	ld	(sva),a		;a sichern
	EX	(SP),HL		;Adresse hinter CALL
db1:	LD	A,(HL)
	CALL	dispc
	INC	HL
	ld	a,'$'
	cp	(hl)
	JR	nz,db1		
	inc	hl
	EX	(SP),HL		;neue Returnadresse
	ld	a,(sva)		;a restoren
	RET


;---------------------------------------------------------------------------------
;subroutine wdstr
;wandelt word in string
;hl=word, um das es geht
;de=Adresse des Zielbereichs (4 Byte)
wdstr:	push 	af
	push	de
	ld	a,h
	call	strbt
	ld	a,l
	call	strbt
	pop	de
	pop	af
	ret

;---------------------------------------------------------------------------------
;subroutine strbt
;store byte as string
;a=byte
;de=adresse
;<-de=adresse + 2
strbt:	push 	af		
	srl  	a
      	srl  	a
      	srl  	a
      	srl  	a
      	call 	strn
      	pop  	af
      	push 	af
      	and  	0fh
	inc	de
      	call 	strn
      	pop  	af
	inc	de
      	ret  	

;subroutine strn
;analog zu outn, nur keine Ausgabe, sondern als String an Stelle (de) speichern
strn:	cp   	0ah
      	jr   	c,strnu
      	add  	a,37h
      	jr   	strnp
strnu: 	add  	a,30h
strnp: 	ld	(de),a
	ret




memrow:	db	0
memcol:	db	0
txcrlf: db	0dh,0ah,24h
txdpsp: db	': ',24h
txhl:	db	' HL=$'
txde:	db	' DE=$'
txbc:	db	' BC=$'
txra:	db	' A=$'
sva:	db	00

;	call	dbg
;	db	'test$'