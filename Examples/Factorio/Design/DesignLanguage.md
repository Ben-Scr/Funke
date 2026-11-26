# Rust & Steel

Weathered orange steel machinery, charcoal rubber belts, pale iron, copper-orange plates, glowing furnace interiors and muted olive-brown earth. Nearly top-down orthographic sprites, subtle vertical faces and light from the upper left. The original ImageGen machine/resource atlas and terrain are reused unmodified.

Square two-unit cells. Belt geometry is generated at exact edge midpoints, so adjoining tiles meet without gaps. Each tile's input/output mask chooses a straight, corner or merging shape. Rubber slats, steel rims and rust edges use the palette tokens. Inserter bases use the same steel/rust/amber family; a separate arm rotates between pickup and delivery, carrying the actual item.

UI uses Rubik, steel frames, restrained bevels/rivets, amber selection and warm text. The top toolbar shows mode, active craft and inventory/help/pause access. The bottom quickbar uses machine icons and owned counts. Notices expire; context describes only the hovered object or selected building. Controls live in on-demand help.

Inventory: 8 x 5 stack grid on the left, 4 x 3 recipe grid on the right. Recipe cards show the item and output count, with material/time details on hover. Unaﬀordable recipes dim their icons. Both modes share this entire layout; Creative replaces costs and time with an instant/free label. Machine contents and transfer buttons sit below; selecting a stack determines what Insert selected sends.

Reference 1600 x 900, Fit scaling and edge anchors. Panels use 9-slice frames; icons preserve aspect ratios. Placement uses a translucent validity tint. Deposits fade when exhausted. Machine status combines an indicator with contextual status text.

Design/palette.json remains authoritative for authored UI, mechanical art and indicators. Painterly source sprites contain intermediate material colors; no strict pixel-art palette is claimed.
