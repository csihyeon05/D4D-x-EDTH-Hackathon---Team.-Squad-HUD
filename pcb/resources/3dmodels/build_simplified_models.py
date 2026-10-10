"""Visual package approximations, not manufacturer-certified mechanical models.

Run with CadQuery. Dimensions are in mm; origins match the assigned footprints.
Do not use these models to approve enclosure or connector mating clearances.
"""
from pathlib import Path
import cadquery as cq

ROOT = Path(__file__).parent
BLACK = cq.Color(0.12, 0.12, 0.14)
SILVER = cq.Color(0.72, 0.74, 0.76)
GREEN = cq.Color(0.08, 0.35, 0.16)


def box(x, y, z, cx=0, cy=0, bottom=0):
    return cq.Workplane('XY').box(x, y, z).translate((cx, cy, bottom + z / 2))


def save(path, parts):
    assembly = cq.Assembly(name='SIMPLIFIED_NOT_MANUFACTURER_MODEL')
    for name, shape, color in parts:
        assert shape.val().isValid(), name
        assembly.add(shape, name=name, color=color)
    output = ROOT / path
    output.parent.mkdir(parents=True, exist_ok=True)
    assembly.export(str(output))
    check = cq.importers.importStep(str(output))
    assert check.val().isValid(), path
    print(path)


# A recognizable connector envelope, not a mating/contact-detail model.
housing = cq.Workplane('XY').polyline([
    (-2.6, -1.3), (-0.9, -2.6), (7.6, -2.6), (7.6, 2.6),
    (-0.9, 2.6), (-2.6, 1.3)
]).close().extrude(8)
for x in (0, 5):
    housing = housing.cut(cq.Workplane('XY').center(x, 0).circle(1.5).extrude(8))
save('Connector_AMASS.3dshapes/AMASS_XT30UPB-M_1x02_P5.0mm_Vertical.step', [
    ('housing_approximate', housing, cq.Color(0.95, 0.72, 0.05)),
    ('pin_1_approximate', cq.Workplane('XY').circle(1).extrude(7.5), SILVER),
    ('pin_2_approximate', cq.Workplane('XY').center(5, 0).circle(1).extrude(7.5), SILVER),
    ('pcb_tail_1', cq.Workplane('XY').circle(0.75).extrude(-3), SILVER),
    ('pcb_tail_2', cq.Workplane('XY').center(5, 0).circle(0.75).extrude(-3), SILVER),
])
save('Inductor_SMD.3dshapes/L_Bourns-SRN6028.step', [
    ('body', box(6, 6, 2.8), BLACK),
])
save('OptoDevice.3dshapes/Maxim_OLGA-14_3.3x5.6mm_P0.8mm.step', [
    ('body', box(3.3, 5.6, 1.55), BLACK),
])
save('RF_GPS.3dshapes/ublox_NEO.step', [
    ('substrate', box(12.2, 16, 0.8), GREEN),
    ('shield_envelope', box(12.2, 16, 1.6, bottom=0.8), SILVER),
])
save('Package_DFN_QFN.3dshapes/SiliconLabs_QFN-20-1EP_3x3mm_P0.5mm_EP1.8x1.8mm.step', [
    ('body', box(3, 3, 0.75), BLACK),
])
