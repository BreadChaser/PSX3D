/* Renderer-only call order. Other .text input sections keep their link order.
 * The fixed crt0 entry stays in .text; 32B aligns the first cache line.
 * No whole-application section-ordering file is used. */
SECTIONS
{
 .text.pica_hot ALIGN(32) :
 {
  __pica_hot_start = .;
  *(.pica_hot.do_cmd_buffer)
  *(.pica_hot.do_cmd_buffer_)
  *(.pica_hot.renderer_do_cmd_list)
  *(.pica_hot.renderer_do_cmd_list_impl)
  *(.pica_hot.draw_poly_impl)
  *(.pica_hot.texture_for_impl)
  *(.pica_hot.texture_for_slow)
  *(.pica_hot.mark_drawn)
  *(.pica_hot.mark_drawn_slow)
  *(.pica_hot.use_state)
  *(.pica_hot.emit_tris)
  __pica_hot_end = .;
 }
}
INSERT AFTER .text;
