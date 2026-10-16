;------------------------------------------------------------------------

m309: 	ld	a,64h		;Warteschleife
m202:	ld	b,0bch
m201:	djnz	m201
	dec	a
	jr	nz,m202
	ret

mxx: 	in	a,(0d5h)	;baustein2 portb daten
	bit	4,a
	jr	z,myy
	ld	a,09
mzz:	out	(0d4h),a	;baustein2 porta daten
	ret

myy: 	bit	3,a
	ld	a,06
	jr	nz,mzz
	call	0730h		;Ausgabe 'GERAETEFEHLER'
	call	lda03		;Byte 03 auf d4 schreiben (baustein2 porta daten)
	jp	0441h		;Sternchen schreiben

lda03: 	ld	a,03		;für Ausgabe an baustein2 porta daten
	jr	mzz

castat:	xor	a
	out	(0d4h),a
	ld	c,0ah
m101:	call	m309		;05c3h		;Warteschleife
	dec	c
	jr	nz,m101		;Warteschleife
	jr	mxx		;Rücksprung Geraetefehler ja/nein
		
		
