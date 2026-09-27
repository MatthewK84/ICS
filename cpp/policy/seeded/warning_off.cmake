# expect: :4:
# Seeded violation (ICS-005): a flag that turns a warning off. The
# suppression scan must report line 4. See CMakeLists.txt.
add_compile_options(-Wno-conversion)
