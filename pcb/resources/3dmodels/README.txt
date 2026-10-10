3D model provenance and limitations
===================================

Standard STEP files were downloaded from the official KiCad model repository:
https://gitlab.com/kicad/libraries/kicad-packages3D
They retain their embedded licensing and provenance information.

U1 uses the detailed Ultra Librarian model NUCODE.3dshapes/NCRB54N01VC.STEP,
downloaded for Nucode NCRB54N01C on 2026-10-11:
https://app.ultralibrarian.com/details/fb7e7e9d-47cf-11f1-aeac-024899f9dfe1/Nucode/NCRB54N01C
The original archive is resources/ul_NCRB54N01C_STEP.zip; its readme is retained.
The STEP is unmodified, with 150 solids and a 10.5 x 16.5 mm XY envelope.
Footprint model offset: (-5.255, -6.168, 0.84698078) mm; scale 1; rotation 0.
This aligns the substrate bottom with the mounting plane and the antenna with
the negative-Y end of the supplied footprint. The old NU54_simplified.step
is retained only as an unused historical asset.

Five files are locally generated SIMPLIFIED visual models, NOT manufacturer CAD:
- Connector_AMASS.3dshapes/AMASS_XT30UPB-M_1x02_P5.0mm_Vertical.step (J1)
- Inductor_SMD.3dshapes/L_Bourns-SRN6028.step (L1)
- OptoDevice.3dshapes/Maxim_OLGA-14_3.3x5.6mm_P0.8mm.step (U2)
- RF_GPS.3dshapes/ublox_NEO.step (U4)
- Package_DFN_QFN.3dshapes/SiliconLabs_QFN-20-1EP_3x3mm_P0.5mm_EP1.8x1.8mm.step (U7)

These show approximate package occupancy only. Terminals, optical apertures,
antenna features and mating surfaces are omitted or simplified. In particular,
the XT30 geometry must not be used for mating/enclosure clearance approval.
Use manufacturer drawings and verified models for mechanical sign-off.

Sources for package envelopes:
https://www.nordicsemi.com/Products/nRF54L05/Modules
https://www.analog.com/media/en/technical-documentation/data-sheets/max30102.pdf
https://www.silabs.com/documents/public/data-sheets/cp2102n-datasheet.pdf
https://content.u-blox.com/sites/default/files/NEO-M8_DataSheet_%28UBX-13003366%29.pdf
The local KiCad footprints provide XY outlines and mounting origins.

build_simplified_models.py regenerates the five simplified models with CadQuery.
No copper pads, electrical pin numbers or component values are changed by it.
