# Rust & Steel

Weathered orange steel machinery, charcoal rubber belts, pale iron, glowing furnace interiors and muted olive-brown earth. Nearly top-down orthographic sprites, subtle vertical faces and light from the upper left. ImageGen supplies the machine/resource atlas. No copied Factorio artwork.

World: square two-unit cells; machines occupy one cell, item visuals are small enough to remain distinguishable on belts. East is default output; rotate by quarter turns. Conveyors follow direction, buildings retain their upright rendered silhouette with a separate output arrow.

UI: Rubik, steel frames with restrained bevels and rivets, amber selected state, pale warm text. Text and values remain live. A compact top contract strip shows gears delivered, budget and time. The bottom construction tray reuses actual machine silhouettes and shows costs. A contextual hover panel describes the selected machine or deposit. Controls are in an optional help panel. No permanent control wall.

Panels use 9-slice frames, icons preserve square proportions, spacing uses eight-unit increments. Reference 1600 × 900, responsive edge anchors and Fit scaling. Status is represented by a compact machine indicator (working/blocked/waiting) and a short tooltip, not color alone. Placement uses a translucent preview and validity tint. Menu, pause and outcome screens share the same steel frame family.

The palette JSON is authoritative for authored terrain, UI, indicators and effects. Painterly generated art may contain intermediate material colors. No pixel-art palette restriction is claimed.
