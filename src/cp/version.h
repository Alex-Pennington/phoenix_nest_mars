/****************************************************************************
**
** Copyright (C) 2016 - 2024 Timothy Millea <timothy.j.millea@gmail.com>
**
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************************/
#ifndef VERSION_H
#define VERSION_H

// Semantic version - update for releases
#define VERSION_MAJOR 1
#define VERSION_MINOR 0
#define VERSION_PATCH 0

// Stringify helper macros
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

// Build metadata - injected by CI, defaults for local builds
#ifndef GIT_COMMIT_SHORT
#define GIT_COMMIT_SHORT local
#endif

#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif

// Version strings
#define VERSION_STRING_BASE "1.0.0"

#ifdef NDEBUG
#define VERSION VERSION_STRING_BASE
#else
#define VERSION VERSION_STRING_BASE "-dev+" STRINGIFY(GIT_COMMIT_SHORT) "." STRINGIFY(BUILD_NUMBER)
#endif

// Release channel
// #define RELEASE_CHANNEL "alpha"
// #define RELEASE_CHANNEL "beta"
#define RELEASE_CHANNEL "release"

#define ALPHA_TIME 39
#define BETA_TIME 99

#endif // VERSION_H
