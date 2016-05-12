/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 *
 * Contributor(s): Sebastian Barschkis (sebbas)
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file mantaflow/intern/strings/shared_script.h
 *  \ingroup mantaflow
 */

#include <string>

const std::string manta_import = "\
from manta import *\n\
import os, shutil, math, sys, gc\n";

const std::string solver_low = "\n\
# solver low params\n\
dim    = $SOLVER_DIM$\n\
res    = $RES$\n\
gs     = vec3($RESX$,$RESY$,$RESZ$)\n\
if dim == 2: gs.z = 1\n\
s      = Solver(name='main', gridSize=gs, dim=dim)\n";

const std::string adaptive_time_stepping = "\n\
# adaptive time stepping\n\
dt_default    = 0.1\n\
dt_factor     = $DT_FACTOR$\n\
fps           = $FPS$\n\
dt0           = dt_default * (25.0 / fps) * dt_factor\n\
s.frameLength = dt0\n\
s.timestepMin = dt0 / 10\n\
s.timestepMax = dt0\n\
s.cfl         = 4.0\n\
s.timestep    = dt0\n";

