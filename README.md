# lxlocale

Een klein GTK+3 programma om de taal (locale) en de toetsenbordindeling in te
stellen, opgezet zoals `lxinput`: één venster, direct resultaat, geen daemon,
geen desktopomgeving nodig.

## Bouwen

    make

Nodig om te bouwen: `libgtk-3-dev` en `pkg-config`.
Nodig om te draaien: `setxkbmap` (pakket `x11-xkb-utils` of `x11-xserver-utils`)
en `xkb-data` voor de lijst met indelingen.

Installeren (optioneel):

    sudo make install          # naar /usr/local

## Wat het doet

**Tab Taal** — kiest `LANG` uit de locales die op dit systeem gegenereerd zijn
(`locale -a`). Daarnaast een aparte keuze voor de opmaak (`LC_TIME`,
`LC_NUMERIC`, `LC_MONETARY`, `LC_PAPER`, `LC_MEASUREMENT`, `LC_ADDRESS`,
`LC_TELEPHONE`, `LC_NAME`, `LC_IDENTIFICATION`). Zo kun je bijvoorbeeld een
Engelse interface draaien met Nederlandse datums en decimale komma's.

Staat de gewenste taal er niet bij, dan is de locale nog niet gegenereerd:

    sudo sed -i 's/^# *nl_NL.UTF-8/nl_NL.UTF-8/' /etc/locale.gen
    sudo locale-gen

**Tab Toetsenbord** — model, indeling en variant, plus een optionele tweede
indeling met een wisseltoets (bijvoorbeeld `nl` + `us` met Alt+Shift). De lijst
komt uit `/usr/share/X11/xkb/rules/evdev.lst`. Elke wijziging wordt meteen met
`setxkbmap` doorgevoerd, zodat je hem in het testveld kunt uitproberen. Sluit je
het venster zonder op Toepassen te klikken, dan wordt de oorspronkelijke
indeling teruggezet.

**Tab Opslaan in** — hier bepaal je waar de instelling blijvend terechtkomt.
Dat is bewust een keuze en geen automatisme, omdat het per opstartmethode
verschilt welk bestand wordt ingelezen:

| Bestand | Wanneer relevant |
|---|---|
| `~/.profile` | tekstsessies en login shells; alleen de taalvariabelen |
| `~/.xprofile` | wordt door de meeste displaymanagers (LightDM, SDDM, GDM) ingelezen vóór de window manager start |
| `~/.xinitrc` | bij `startx`; het blok komt vóór de eerste `exec`-regel |
| `~/.fluxbox/startup` | idem, voor wie Fluxbox via dit bestand start |
| `~/.config/environment.d/60-lxlocale.conf` | systemd-gebruikerssessies |
| systeembreed | `localectl set-locale` en `localectl set-x11-keymap` via `pkexec` |

`~/.xinitrc` en `~/.fluxbox/startup` worden alleen aangepast als ze al bestaan.
Een nieuw aangemaakt `.xinitrc` zonder `exec <window manager>` zou `startx`
namelijk onbruikbaar maken.

De taalinstelling wordt pas actief bij de volgende aanmelding; de
toetsenbordindeling is meteen actief.

## Hoe het schrijft

Alles wat lxlocale in bestaande bestanden zet, staat tussen twee markeringen:

    # >>> lxlocale begin >>>
    export LANG='nl_NL.UTF-8'
    setxkbmap -model pc105 -layout nl -option ''
    # <<< lxlocale end <<<

Bij een volgende keer Toepassen wordt precies dat blok vervangen; de rest van
het bestand blijft ongemoeid en het bestand groeit niet aan. Wil je lxlocale
weer kwijt, dan verwijder je het blok met de hand.

## Beperkingen

- X11 only. Onder Wayland doet `setxkbmap` niets; daar stelt de compositor de
  indeling in en is alleen de systeembrede `localectl`-route zinvol.
- Één wisseltoets (`grp:`-optie) tegelijk; andere xkb-opties (compose, caps)
  zijn niet ingebouwd, maar zijn er eenvoudig bij te zetten in
  `build_keyboard_page()` door de filter op `grp:` los te laten.
- De interface is Nederlandstalig en niet vertaald; de teksten staan als letterlijke
  strings in `lxlocale.c`.

## Licentie

3-Clause BSD
