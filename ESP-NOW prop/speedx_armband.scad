// SpeedX wearable enclosure — upper-arm band
// Sized for the BOM in HARDWARE.md. This is an ARMBAND: the NEO-M8N GPS,
// its 25mm patch antenna and an 18650 do NOT fit a slim wrist strap.
//
// Coordinates:  X = along the arm (length)   Y = around the arm (width)
//               Z = up, away from skin       z=0 is the skin-side base
// The strap exits the two long (±Y) sides and wraps around the arm.
//
// Render F6, then export each part. Set `part` below. Lay flat = no supports
// except the strap-slot bridges (or print the body on a long side).
// Every number here is a knob — re-measure YOUR parts and change them.

part = "all";           // "body" | "lid" | "both" | "pad" | "footpod" | "all"

/* ---- internal cavity = the component stack -----------------------------
   Measured-ish footprints (mm), lay them out before trusting these:
     LOLIN D32   ~55 x 26 x 13   (ESP32 + LiPo charger, USB-C on one end)
     NEO-M8N     ~36 x 26 + 25x25x8 ceramic antenna  (antenna faces UP/out)
     MPU-6050    ~21 x 16 x 3
     MAX30102    ~14 x 14 x 3     (lens faces DOWN to skin)
     18650       65 x 18 dia      (or 1S LiPo 503450 = 50x34x5 -> height 16) */
inner_length = 70;      // along arm  (fits an 18650 end-to-end + clearance)
inner_width  = 46;
inner_height = 16;      // flat 1S LiPo (e.g. 503450); use 22 for an 18650

wall    = 2.0;
floor_t = 2.0;          // base thickness under the cavity
corner  = 4;            // inside corner radius

arm_radius = 45;        // concave underside: wrist ~28, upper arm ~45, big ~55

// strap (25mm velcro threads through a closed slot on each long side)
strap_w   = 26;         // slot width  (strap + clearance), runs along X
strap_gap = 3.5;        // slot height (strap thickness + clearance)
lug_out   = 6;          // how far the strap rib sticks out in Y
bar_t     = 1.6;        // wall above/below the strap slot

// skin sensor window (MAX30102) at the centre of the base
window    = [15, 15];
sensor_nub = 1.2;       // base ring protrudes this much so the lens meets skin

// USB-C charge port through the +X end wall (LiPo charging without opening)
usb       = [11, 5];    // w, h of the cutout
usb_z     = 6;          // centre height above the floor

// lid retention: M2 self-tapping screws into 4 corner bosses
screw_d   = 1.7;        // pilot in the boss (self-tap M2)
lid_hole  = 2.4;        // clearance in the lid
boss_d    = 5;
clearance = 0.3;        // lid lip slip-fit
lip_h     = 4;

// GPS: a shelf inside the body cradles the 25mm patch antenna face-up, flush
// under a thinned window in the lid (better sky view than firing through 2mm).
antenna_size = [25, 25];          // patch antenna footprint
antenna_pos  = [(inner_length+2*wall)*0.72, (inner_width+2*wall)/2]; // free area, away from the 18650
antenna_membrane = 0.8;           // plastic left over the antenna in the lid

// TPU skin pad (part="pad"): soft backing that sticks to the underside
pad_t = 2.5;

// foot-pod (part="footpod"): tiny box for a 2nd MPU-6050 on the shoe, for
// ground-contact time. Lace/zip-tie threads the two base slots.
fp_inner = [24, 18, 9];           // MPU-6050 + 401015 LiPo, roughly
fp_wall  = 1.6;

$fn = 48;

// ---- derived ----
out_l = inner_length + 2*wall;
out_w = inner_width  + 2*wall;
body_h = inner_height + floor_t;
sag = arm_radius - sqrt(arm_radius*arm_radius - (out_w/2)*(out_w/2)); // edge drop
skirt_h = sag + 1;
boss_in = wall + boss_d/2 + 0.5;

assert(arm_radius > out_w/2, "arm_radius must exceed half the case width");
assert(inner_height > usb_z + usb[1]/2, "USB port must sit within the cavity");
assert(corner < min(inner_length, inner_width)/2, "corner radius too big");

module rounded_box(l, w, h, r) {
    hull() for (x=[r, l-r], y=[r, w-r]) translate([x, y, 0]) cylinder(h=h, r=r);
}

module screw_positions()
    for (x=[boss_in, out_l-boss_in], y=[boss_in, out_w-boss_in])
        translate([x, y, 0]) children();

// solid strap rib on one long side; cut() = the slot through it
module strap_rib(yface, dir) {
    translate([out_l/2 - strap_w/2 - wall, yface, 0])
        cube([strap_w + 2*wall, lug_out, strap_gap + 2*bar_t]);
}
module strap_slot(yface) {
    translate([out_l/2 - strap_w/2, yface - 1, bar_t])
        cube([strap_w, lug_out + 2, strap_gap]);
}

module body() {
    difference() {
        union() {
            rounded_box(out_l, out_w, body_h, corner + wall);          // shell
            translate([0, 0, -skirt_h])                                // arm-hug skirt
                rounded_box(out_l, out_w, skirt_h, corner + wall);
            screw_positions() translate([0,0,floor_t]) cylinder(h=inner_height, d=boss_d);
            strap_rib(out_w, 1);                                       // +Y rib
            translate([0, -lug_out, 0]) strap_rib(0, -1);             // -Y rib
            if (antenna_size[0] > 0) antenna_shelf();                  // GPS patch cradle
            // sensor contact ring on the underside
            translate([out_l/2, out_w/2, -sensor_nub])
                linear_extrude(sensor_nub)
                    offset(2) square([window[0], window[1]], center=true);
        }
        // cavity
        translate([wall, wall, floor_t])
            rounded_box(inner_length, inner_width, inner_height + 1, corner);
        // concave arm curve (axis along X, centre below the part -> scoops the skirt)
        translate([out_l/2, out_w/2, -arm_radius]) rotate([0,90,0])
            cylinder(h = out_l + 2*lug_out + 40, r = arm_radius, center=true, $fn=120);
        // skin window straight through base + nub
        translate([out_l/2, out_w/2, -sensor_nub - 1])
            cube([window[0], window[1], floor_t + sensor_nub + 3], center=true);
        // USB-C port through +X wall
        translate([out_l - wall - 1, out_w/2 - usb[0]/2, floor_t + usb_z - usb[1]/2])
            cube([wall + 2, usb[0], usb[1]]);
        // strap slots
        strap_slot(out_w);
        translate([0, -lug_out, 0]) strap_slot(0);
        // screw pilots
        screw_positions() translate([0,0,floor_t]) cylinder(h=inner_height + 0.1, d=screw_d);
    }
}

module lid() {
    difference() {
        union() {
            rounded_box(out_l, out_w, wall, corner + wall);           // top plate
            translate([wall + clearance, wall + clearance, wall])     // friction lip
                difference() {
                    rounded_box(inner_length - 2*clearance, inner_width - 2*clearance, lip_h, corner);
                    translate([bar_t, bar_t, -1])
                        rounded_box(inner_length - 2*clearance - 2*bar_t,
                                    inner_width  - 2*clearance - 2*bar_t,
                                    lip_h + 2, max(corner - bar_t, 0.6));
                }
        }
        screw_positions() translate([0,0,-1]) cylinder(h=wall + 2, d=lid_hole);
        if (antenna_size[0] > 0)                                      // thinned GPS sky window
            translate([antenna_pos[0] - (antenna_size[0]+1)/2,
                       antenna_pos[1] - (antenna_size[1]+1)/2, antenna_membrane])
                cube([antenna_size[0] + 1, antenna_size[1] + 1, wall + lip_h]);
    }
}

// ---- GPS antenna shelf (in the body) ----
module antenna_shelf() {
    th = 8;                                   // patch + ceramic thickness
    seat = body_h - antenna_membrane - th;    // top of patch ~flush under the lid
    translate([antenna_pos[0], antenna_pos[1], floor_t]) difference() {
        union() {
            difference() {                    // locating collar
                translate([-(antenna_size[0]/2+1.2), -(antenna_size[1]/2+1.2), 0])
                    cube([antenna_size[0]+2.4, antenna_size[1]+2.4, body_h-floor_t]);
                translate([-(antenna_size[0]/2+0.3), -(antenna_size[1]/2+0.3), -1])
                    cube([antenna_size[0]+0.6, antenna_size[1]+0.6, body_h]);
            }
            translate([-(antenna_size[0]/2+0.3), -(antenna_size[1]/2+0.3), seat-floor_t-1.5])
                cube([antenna_size[0]+0.6, antenna_size[1]+0.6, 1.5]);   // seat shelf
        }
        translate([-3, antenna_size[1]/2-1, 0]) cube([6, 4, body_h]);    // cable slot
    }
}

// ---- TPU skin pad (sticks to the underside; hole passes the sensor nub) ----
module skin_pad() {
    difference() {
        rounded_box(out_l, out_w, pad_t, corner + wall);
        translate([out_l/2, out_w/2, -1])
            cube([window[0]+1, window[1]+1, pad_t+2], center=true);
    }
}

// ---- foot-pod: 2nd MPU-6050 on the shoe (ground-contact time) ----
function fpL() = fp_inner[0] + 2*fp_wall;
function fpW() = fp_inner[1] + 2*fp_wall;
function fpH() = fp_inner[2] + fp_wall;
module fp_ear(xface) {                         // lace / zip-tie slot
    difference() {
        translate([xface, fpW()/2-6, 0]) cube([5, 12, fpH()*0.7]);
        translate([xface-1, fpW()/2-4, fpH()*0.25]) cube([7, 8, 2.5]);
    }
}
module footpod() {
    difference() {
        union() {
            rounded_box(fpL(), fpW(), fpH(), 2);
            fp_ear(-5); fp_ear(fpL());
        }
        translate([fp_wall, fp_wall, fp_wall])
            rounded_box(fp_inner[0], fp_inner[1], fp_inner[2]+1, 1.2);
        translate([fpL()-fp_wall-1, fpW()/2-4, fp_wall+2]) cube([fp_wall+2, 8, 4]); // USB
    }
}
module footpod_lid() {
    rounded_box(fpL(), fpW(), 1.5, 2);
    translate([fp_wall+0.3, fp_wall+0.3, 1.5]) difference() {
        rounded_box(fp_inner[0]-0.6, fp_inner[1]-0.6, 3, 1.0);
        translate([1.2, 1.2, -1]) rounded_box(fp_inner[0]-3, fp_inner[1]-3, 5, 0.5);
    }
}

if (part == "body" || part == "both" || part == "all") body();
if (part == "lid"  || part == "both" || part == "all") translate([0, out_w + 15, 0]) lid();
if (part == "pad" || part == "all") translate([0, -(out_w + 15), 0]) skin_pad();
if (part == "footpod" || part == "all")
    translate([out_l + 20, 0, 0]) { footpod(); translate([0, fpW() + 10, 0]) footpod_lid(); }
