	title kmbtest
	.z80			;Z80-Code generieren

	extrn	kmbg

	bdos	equ 5		;Aufruf BDOS-Funktionen
	cr	equ 0dh
	lf	equ 0ah
	home	equ 0ch		;BS loeschen, Kursor home

	org 0100h


	jp	start
	INCLUDE	FS.INC		;file
	INCLUDE	DISP.INC	;display

;
start:	ld	hl,heap
	ld	de,1000h
	add	hl,de
	ld	(rheap),hl	;Heap-Start = Programmende + genug Platz für kmbg.rel

	ld	de,txinit	;home,'Testprogramm fuer kmbg.rel-Funktionen'
	call	disps

	ld	de,txkey	;'Kassette einlegen und Taste druecken'
	call	disps
	call	waitk

        ld	c,15h		;Kassette 1 verriegeln
        call	kmbg		;Funktion aufrufen, im Akku wird Statusbyte zurueckgegeben
    	call	dispra
        bit	2,a		;Bit 2 im Akku=1 -> Geraet nicht bereit
        jp	nz,fehler	;Ausgabe Fehlermeldung und Beenden Prg.
        ld	de,txk1ok	;'Funktion ok '
	call	disps

	ld	de,txk1r	;'Kass. 1 rueckspulen? (J): '
	call	disps



	ld	c,34h		;schnelles Rückspulen
        call	kmbg		;Funktion in kmbg.rel aufrufen
    	call	dispra


        bit	2,a		;Bit 2 im Akku=1 -> Geraet nicht bereit
        jp	nz,fehler	;Ausgabe Fehlermeldung und Beenden Prg.
        ld	de,txk1ok	;'Funktion ok '
	call	disps

nxfile:	ld	hl,(filenr)
	ld	de,nrtp
	call	wdstr		;Dateiname mit Nr präparieren
	ld	de,fcbtp	
	call	fcreat		;Datei öffnen
	call	dispra

	ld	hl,(rheap)
	ld	de,(rheap)
	inc	de
	ld	bc,8000h
	ld	(hl),00
	ldir			;heap löschen

	ld	de,(rheap)	;startadr
	ld	bc,0000		;Anzahl Bytes

nxblk:	ld	hl,104h		;Blocklänge

	inc	de
	inc	de
	inc	de
	inc	de		;4 Byte Platz lassen für Blocklänge
		
	push	de
	ld	de,txrd1b	;'Lesen 1 Block: '
	call	disps
	pop	de

	push	bc
	push	de
	ld	c,01h		;Lesen 1 Block
        call	kmbg		;Funktion in kmbg.rel aufrufen
	pop	de
	pop	bc

	call	disphl
	call	dispra		;?? a=02h, 0000 0010 blocklängenfehler

	bit	6,a		;EOT erkannt?
	jp	nz,ende
		
	dec	de
	dec	de
	dec	de
	dec	de		
	call	wrblsz		;Blockgröße in die 4 Bytes vor dem Block schreiben
	inc	de
	inc	de
	inc	de
	inc	de		

	call	dispde
	push	hl
	add	hl,de		;Zieladresse um Blocklänge + Präambel/Postambel/crc erhöhen
	ld	d,h
	ld	e,l
	call	dispde
	pop	hl
	
	ld	(de),a		;Statusbyte schreiben
	inc	de		;Adresse + 1
	
	call	wrblsz		;Blockgröße in die 4 Bytes direkt nach dem Block + Präambel/Postambel/crc schreiben
	inc	de
	inc	de
	inc	de
	inc	de		
	call	dispde


	call	dispbc
	add	hl,bc		;Anzahl gelesene Bytes erhöhen
	ld	bc,0009		
	add	hl,bc		;noch 2x Blocklänge + 1x Statusbyte mehr
	ld	b,h
	ld	c,l
	call	dispbc

	call	wrheap		;check, ob heap erreicht, wenn ja, wegschreiben, Überhang > 8000h umkopieren, Pointer zurücksetzen	

	bit	5,a		;Bandmarke erkannt?
	jr	z,nxblk		;wenn nein, D5=0, nächster Block

	call	swfile		;Heap wegschreiben, Datei schließen, Tapefilenr erhöhen

	call	dbg
	db	'jr nxfile$'
	jp	nxfile


;------------------------------------------------------------------------
;subroutine wrheap (write heap)
;check, ob heap erreicht, wenn ja, wegschreiben, Überhang > 8000h umkopieren, Pointer zurücksetzen
;de=aktuelle Schreibadresse
;bc=Anzahl Bytes

;de=aktuelle Schreibadresse
;bc=Anzahl Bytes

wrheap:	push	af
	push	hl
	push	de
	push	bc

	call	disphl
	call	dispde
	call	dispbc
	
	scf
	ccf
	ld	hl,(rheap)
	push	de
	ld	de,8000h	;hl=32k-Grenze, ab der wir reagieren müssen
	add	hl,de
	pop	de

	sbc	hl,de
	jp	nc,wrhpr	;return, wenn Grenze noch nicht erreicht
	
	ld	hl,(rheap)
	ld	de,fcbtp	
	ld	bc,8000h
	call	fsave
	;TODO: Fehlerhandling

	call	dbg
	db	'clear: $'

	ld	hl,(rheap)
	ld	de,(rheap)
	inc	de
	ld	bc,7fffh

	call	disphl
	call	dispde
	call	dispbc

	ld	(hl),00
	ldir
	;TODO: refaktorisieren

	pop	bc
	pop	de

	call	dbg
	db	'sub bc: $'

	scf
	ccf
	ld	h,b
	ld	l,c
	ld	bc,8000h
	sbc	hl,bc
	ld	b,h
	ld	c,l		;bc=Anzahl restliche Bytes, die umkopiert werden müssen
	call	dispbc

	call	dbg
	db	'sub de: $'

	scf
	ccf
	ld	h,d
	ld	l,e		;de=neuer Heapzeiger
	ld	de,8000h
	sbc	hl,de
	ld	d,h
	ld	e,l		;de=neuer Heapzeiger
	call	dispde

	push	de		;neuen de-Wert auf Stack sichern zwecks späterer Rückgabe
	push	bc		;neuen bc-Wert auf Stack sichern zwecks späterer Rückgabe
	

	call	dbg
	db	'copy: $'
	
	ld	hl,(rheap)
	ld	de,8000h
	add	hl,de
	ld	de,(rheap)

	call	disphl
	call	dispde
	call	dispbc

	ldir

wrhpr:	pop	bc
	pop	de
	pop	hl
	pop	af
	ret

;------------------------------------------------------------------------
;subroutine wrblsz
;schreiben Blockgröße wie in Simh
;de=zieladr
;hl=blocksize

wrblsz:	push	af
	push	de
	
	ld	a,l
	ld	(de),a

	inc	de		
	ld	a,h
	ld	(de),a

	inc	de		
	ld	a,0
	ld	(de),a

	inc	de		
	ld	a,0
	ld	(de),a

	pop	de
	pop	af
	ret

;------------------------------------------------------------------------
;subroutine swfile (switch file)
;Heap wegschreiben, Datei schließen, Tapefilenr erhöhen
swfile:	push	hl

	ld	hl,(rheap)
	ld	de,fcbtp	
	call	fsave

	call	dispra

	ld	de,fcbtp	
	call	fclose

	call	dispra

	ld	hl,(filenr)	;tapefilenr erhöhen
	inc	hl
	ld	(filenr),hl

	call	disphl

	pop	hl
	ret

ende:   call	swfile		;Rest noch wegschreiben

	ld	de,txk1e	;'Taste Kass. 1 entriegeln: '
	call	disps

        ld	c,16h		;Funktionscode "entriegeln"
        call	kmbg		;externe Funktion aufrufen
        bit	2,a		;Ergebnis pruefen
        jr	nz,fehler	;Fehlermeldung ausgeben
        ld	de,txkenr	;'Kassette 1 entriegelt !'
	call	disps
        jr	quit		;und Kaltstart

fehler:	ld	de,nxdrnr	;'Laufwerk nicht bereit !'
	call	disps
quit:	ld	de,txquit	;Abschiedsmeldung
	ld	c,9		;ausgeben
        call	5
        ld	c,1
        call	5		;auf Taste warten
	jp	0		;und Restart CCP
;
txinit:	db	home,'Testprogramm fuer kmbg.rel-Funktionen',cr,lf,24h
txkey:	db	cr,lf,'Kassette einlegen und Taste druecken: ',24h
nxdrnr:	db	cr,lf,'Laufwerk nicht bereit !',24h
txrd1b:	db	cr,lf,'Lesen 1 Block: ',24h
txk1e:	db	cr,lf,'Taste Kass. 1 entriegeln: ',24h
txk1r:	db	cr,lf,'Kass. 1 rueckspulen: ',24h
txk1ok:	db	cr,lf,'Funktion ok ',24h
txkenr:	db	cr,lf,'Kassette 1 entriegelt !',24h
txquit:	db	cr,lf,'Taste fuer Restart',24h
filenr:	dw	0
fcbtp:	db	0
	db	'TAPE'
nrtp:	db	'    '		;Tapefile-Nummer
	db	'BIN'
	ds   	24,0		;buffer für fcb der Temporärdatei
                        	
; ---------------------------------------------------------------------------

rheap:	dw	0000
heap:	db	0
	end
                                                                                                  
                                                                                                  