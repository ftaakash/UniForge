# V2 report: figure tokens

Every figure in the V2 report uses these values (build spec §12.2, review amendment A13), so the
same solver looks the same in every plot. Change a token here, not in a single figure.

## Solvers

| Family | Colour | Variant | Line | Marker |
|---|---|---|---|---|
| Direct (Cholesky) | `#0072B2` blue | `DIRECT-ND` | solid | ● filled circle |
| | | `DIRECT-RCM` | dashed | ○ hollow circle |
| PCG | `#E69F00` orange | `PCG-J` | solid | ● filled circle |
| | | `PCG-IC0` | dashed | ■ filled square |
| AMG | `#009E73` green | `AMG` | solid | ▲ filled triangle |
| | | `AMG-PCG` | dashed | △ hollow triangle |
| Theory / reference lines | grey | – | dashed, thinner | none |

The colours are from the Okabe–Ito colour-blind-safe palette [VERIFY hex values against the
source before the report is built].

## Temperature fields

- Colormap: inferno (as in v1), low = dark, high = bright.
- Legend in °C with min and max ticks; all views of one case share one range.

## Axes

- Every scaling plot is log–log; decades labelled 10ⁿ.
- The x axis of a scaling plot is always "DOF" (never mesh spacing or level).
- Each fitted line has its slope printed next to it.

## Failed and capped runs

- Drawn as a hollow ✕ at the size that was attempted, with a short reason ("over 8 GB",
  "30 min cap", "IC(0) breakdown"). Never left out silently.

## Text

- Fonts: Inter for text, JetBrains Mono for numbers in tables (the v1 report fonts).
- Units in every axis label and table header.
- Caption template: *what is shown · case and mesh · what to notice · data file name*.
