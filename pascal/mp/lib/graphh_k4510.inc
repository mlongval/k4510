(* [K4510] GRAPH on VICKY: layer 1, an 8-bit bitmap at $200000 (the one
   BBC BASIC's ULA draws on), pixels through the 45GS02's flat store,
   lines/rectangles/triangles through the blitter. Pixel value 0 is
   transparent -- the console's text shows through -- so the ROM console
   and the picture coexist, as in a BBC MODE. Colours are the machine's
   palette (0-15 the C64 set).

   How big: InitGraph(0) draws at the glass the machine is showing -- in
   one of the panel's integer display resolutions (MODE -s n) that glass,
   720x540 or 1440x1080 or 360x270, up to 1920x1200; in the classic modes
   640x480.  GetMaxX and GetMaxY say which (2026-10-07).  InitGraph(1)
   asks for 640x480 whatever the glass, as the unit always did: in an
   integer display resolution it puts the console in MODE 0 and CloseGraph
   puts it back.  The BBC unit uses 1: its sprites live at $260000, above
   a 640x480 bitmap and inside a bigger one. *)

const
	GFX_BASE = $200000;
	GFX_W = 640;			(* InitGraph(1)'s size; gfx_width and gfx_height are the bitmap's now *)
	GFX_H = 480;
	GFX_GLASS = 0;			(* InitGraph's modes: the glass as it is, *)
	GFX_VGA = 1;			(* or 640x480 *)

var	gfx_width: word = 640;		(* the bitmap's size, set by InitGraph *)
	gfx_height: word = 480;
	WIN_LEFT: smallint = 0;
	WIN_RIGHT: smallint = 79;
	WIN_TOP: smallint = 0;
	WIN_BOTTOM: smallint = 29;

	procedure DrawLine(x0, y0, x1, y1: smallint);
	(* @description: a line by the blitter, in the current colour (Line and LineTo use it) *)
	procedure FillBar(x0, y0, x1, y1: smallint);
	(* @description: a filled rectangle by the blitter, corners in any order *)
	procedure FillTriangle(x0, y0, x1, y1, x2, y2: smallint);
	(* @description: a filled triangle by the blitter *)
	procedure ClearDevice;
	(* @description: the bitmap cleared to transparent (the text shows) *)
