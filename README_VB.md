# Doom para Virtual Boy (port de GBADoom / prBoom)

Motor Doom real (GBADoom de doomhack, derivado de prBoom, GPL v2) portado a la NEC V810.
WAD: Doom shareware (Episodio 1) embebido en la ROM. ROM de 4 MB (HyperFlash32).

## Compilar (Linux)
Toolchain v810-gcc 4.7.4 (VUEngine-Studio-Tools, carpeta linux/gcc).

    make TC=/ruta/a/gcc/bin

(VB_ROM1WAIT: ROM con 1 ciclo de espera; probado en hardware real con HyperFlash32.)

Salida: doomreal.vb

## Cambios principales respecto a GBADoom
- vb/i_system_vb.c: capa de plataforma VB (VIP en modo framebuffer, mando, temporizador por interrupción de VIP).
- vb/vb_fb.c + include/vb_fb.h: render directo a 2 bpp en formato de framebuffer VB (240x160, 9,6 KB).
- include/vb_mem.h + source/z_zone.c: zona de memoria repartida entre la WRAM libre y la memoria del VIP
  que no se usa (CHR, BGMaps, OAM), con centinelas por región y asignación best-fit.
- p_setup.c/p_mobj.c: el pool de objetos solo reserva los que aparecen en la dificultad elegida.
- m_fixed.h: FixedMul con MUL de 64 bits de la V810 y FixedDiv sin división de 64 bits; sin tabla de recíprocos (256 KB).
- Límites: MAXDRAWSEGS 128, MAXVISSPRITES 64. Sin efecto "melt" entre pantallas.
- vb/real.ld: datos en WRAM, sin usar la SRAM del cartucho.

## Sonido
- Música: partituras MUS originales del shareware (comprimidas LZSS) tocadas en el VSU: canales 1-4 melodía, canal de ruido percusión.
- Efectos: los efectos digitalizados originales (lumps DS*), remuestreados a 5,5 kHz y 4 bits, en el canal 5 usado como DAC (2 voces mezcladas, estéreo). Interrupción de temporizador en ensamblador (vb/vb_timer_isr.s).
- Datos generados con audio/make_audio.py desde doom1.wad.

## Controles
Cruceta izq.: andar/girar · Cruceta der. izq/der: strafe · Gatillos L/R: disparar
A: cambiar de arma (siguiente) · B: usar/abrir (mantener = correr)
Start: menú (A acepta) · Select: mapa

## Menú y partidas
- Menú principal: SELECT LEVEL (última opción, con la fuente de menú de Doom: parche M_LVLSEL
  compuesto con letras de otros parches del menú) -> E1M1-E1M9 -> dificultad.
- Guardar/Cargar: 8 ranuras en la SRAM del cartucho (un byte por media palabra en 0x06000000).
  Como en GBADoom, se guarda el nivel, la dificultad, las armas y la munición; al cargar se empieza
  ese nivel desde el principio con ese equipo.
