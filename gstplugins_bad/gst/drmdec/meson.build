gst_drmdec_elements_sources = [
  'gstdrmdec.c',
  'gstdrmelements.c'
]

gst_drmdec = library('gstdrmdec',
  gst_drmdec_elements_sources,
  dependencies : [gst_dep, gst_base_dep],
  include_directories : [configinc],
  c_args : gst_c_args,
  install : true,
  install_dir : plugins_install_dir)
