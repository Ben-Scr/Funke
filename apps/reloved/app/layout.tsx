import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Reloved — Dein Resale Studio",
  description: "Dein Inventar. Alles im Blick. Lieblingsstücke verwalten und neu einstellen.",
  icons: {
    icon: "/favicon.svg",
    shortcut: "/favicon.svg",
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="de">
      <body className="antialiased">{children}</body>
    </html>
  );
}
