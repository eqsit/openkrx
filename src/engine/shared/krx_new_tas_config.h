// KRX 1.36.1 config registration, 0x140063ec3..0x14006481c.
// Included repeatedly by config macro consumers.
MACRO_CONFIG_INT(KrxTasAnalyzerAnalyze, krx_tas_analyzer_analyze, 0, 0, 1, CFGFLAG_CLIENT, "Analyze replay stability across hook fire speeds")
MACRO_CONFIG_INT(KrxTasAnalyzerSpeed, krx_tas_analyzer_analyze_speed, 7900, 0, 8100, CFGFLAG_CLIENT, "Reference TAS hook fire speed in hundredths")
MACRO_CONFIG_INT(KrxTileEditorEnable, krx_tile_editor_enable, 0, 0, 1, CFGFLAG_CLIENT, "Enable tile editor and show its tunnels and finish points")
MACRO_CONFIG_INT(KrxTileEditorType, krx_tile_editor_type, 0, 0, 1, CFGFLAG_CLIENT, "Selected editor tile type (0: Tunnel, 1: Finish)")
MACRO_CONFIG_INT(KrxTileEditorClear, krx_tile_editor_clear, 0, 0, 1, CFGFLAG_CLIENT, "Clear all editor tiles")
MACRO_CONFIG_INT(KrxTileEditorAutoTunnel, krx_tile_editor_auto_tunnel, 0, 0, 1, CFGFLAG_CLIENT, "Create automatic tunnels from TAS replay")
MACRO_CONFIG_INT(KrxTileEditorAutoTunnelWidth, krx_tile_editor_auto_tunnel_width, 2, 0, 10, CFGFLAG_CLIENT, "Width of the automatic tunnel")
MACRO_CONFIG_INT(KrxTileEditorAutoFinish, krx_tile_editor_auto_finish, 0, 0, 1, CFGFLAG_CLIENT, "Find and add map finish tiles to the editor")
