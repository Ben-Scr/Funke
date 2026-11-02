# World-space UI

Add a `Transform2D` or `Transform3D` alongside `RectTransform2D` on a UI entity to place its UI hierarchy in the world. The two world transform types remain mutually exclusive.

- The world transform controls position, rotation and scale.
- The RectTransform controls UI size, pivot, anchors and local layout. Child UI entities need only their usual RectTransform and widget components.
- At unit world scale, 100 UI units occupy one world unit. World UI size does not change with the window resolution or safe area.
- A RectTransform without a world-transform canvas ancestor remains screen-space UI.

Images, text and widgets use the canvas transform for drawing. Mouse and touch hit tests project the pointer onto that same canvas plane; dragged widgets keep using their canvas coordinates. The active 3D camera is used when present, otherwise the main 2D camera.

In the Editor View, select the canvas to manipulate its world transform. Select a child to edit its local UI layout; the Rect tool edits its size. The 3D view also supports picking and framing world-space UI.

World-space images and text depth-test against scene meshes, so opaque geometry in front hides them. They render before postprocessing and MSAA resolve, without writing depth, preserving UI blending and canvas ordering. Screen-space UI is drawn over world-space canvases. Separate world canvases have stable canvas order; sorting layer/order controls elements within each canvas.
