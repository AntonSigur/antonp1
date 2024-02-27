# AntonP1 - DMSR Parser kóði
"Dynamic" OBIS Code reader and parser, optimized for safe memory usage in ESP, no memory fragmentation.

DMSR parser "Slimme-Meter"

Aðal hugmyndin er að byggja góðan parser, sem getur lesið P1 Port á snjallmælum frá Veitum en er nægilega flex til að geta
lesið alla mæla sem fylgja DMSR staðlinum, alla OBIS kóða sem mælirinn skilar.

Nýta kóðan til að búa til frístandandi vefþjón, senda sem MQTT skeyti áfram.

Afhverju gerði ég þetta:Þ Ég byrjaði að smíða þennan pakka til að geta sent gögn á Azure-IoT Hub og þaðan áfram í notkun í mælaborðum og örðum Azure tólum. Það reyndist mjög erfitt að nýta ESP8266 til að framkvæma allar aðgerðir, Azure kóðinn þarf mjög mikið minni til að dulkóða gögnin og tengjast Azure o.s.frv. - Tilbúnar lausnir og kóði sem ég fann var yfirleitt frekar takmarkaður og útlistaði ekki alla OBIS kóða eða var með óþarfa gáfum í kringum parsing til að mappa inn í sértæk nöfn o.þ.h.  Þetta ýtti mér í að gera P1/DMSR parserinn þannig úr garði gerðan að hann myndi hafa sem minnst áhrif á minnisnotkun á ESP8266 og skila öllum gögnum úr skeytinu áfram. Það tókst. 

# Notkun
Til að gefa smá mynd af því hvernig þetta er notað er hér einnig einfaldur ESP8266 kóði sem birtir gögnin yfir vefþjón. 

WIFI stillingar geta farið í "wifisecrets.h" til að fá tengingu við wifi net

Hægt er að "telneta" inn á controllerinn til að sjá DEBUG upplýsingar/logga. Þar sem HW Serial er notað í P1 samskiptum er þetta sú
besta lausn sem ég fann til að geta deböggað og séð hvað er að gerast í kóðanum.

undir /api er hægt að sækja payload yfir vefþjón, sem lítur svona út, en er hægt að aðlaga á einfaldan máta eftir þörfum/formi sem hver vill:

```
{
    "Device": {
        "Uptime": 52798,
        "HFB": 20000,
        "HFPct": 2,
        "Version": "1.0.0",
        "Name": "HANANTON"
    },
    "OBIS": [
        {
            "Code": "32.7.0",
            "DValue": 223.6,
            "Unit": "V"
        },
        {
            "Code": "31.7.0",
            "IValue": 0,
            "Unit": "A"
        },
        {
            "Code": "4.7.0",
            "DValue": 0,
            "Unit": "kvar"
        },
        {
            "Code": "3.7.0",
            "DValue": 0,
            "Unit": "kvar"
        },
        {
            "Code": "2.7.0",
            "DValue": 0,
            "Unit": "kW"
        },
        {
            "Code": "1.7.0",
            "DValue": 0.02,
            "Unit": "kW"
        },
        {
            "Code": "2.8.0",
            "DValue": 0,
            "Unit": "kWh"
        },
        {
            "Code": "1.8.0",
            "DValue": 4.107,
            "Unit": "kWh"
        },
        {
            "Code": "0.9.2",
            "IValue": 240227
        },
        {
            "Code": "0.9.1",
            "IValue": 194116
        },
        {
            "Code": "96.1.1",
            "SValue": "36303834303335343534"
        },
        {
            "Code": "96.1.0",
            "IValue": 84035454
        }
    ]
}
```

# Verkefni í vinnslu
Þetta er verkefni í vinnslu sem og þessi documentation. Endilega hjálpið til með góðum viðbótum.
