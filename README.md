# Date iCal

This module allows users to export iCal feeds using Views, and import iCal feeds
from other sites using Feeds. Any entity that contains a Date field can act as
the source/target to export/import an iCal feed.

## Installation

- Install this module using [the official Backdrop CMS instructions](https://backdropcms.org/guide/modules).

## Documentation

- Read and contribute to [documentation in the module's wiki](https://github.com/backdrop-contrib/date_ical/wiki).

## Issues

Bugs and Feature requests should be reported in the [Issue Queue](https://github.com/backdrop-contrib/date_ical/issues).

## Current Maintainers

- John Franklin (https://github.com/sentaidigital/)
- Laryn Kragt Bakker (https://github.com/laryn/)

## Credits

- Ported to Backdrop CMS by [John Franklin](https://github.com/sentaidigital).
- Originally written for Drupal by [Karen Stevenson](https://www.drupal.org/u/karens)
  and [coredumperror](https://www.drupal.org/u/coredumperror).
- The `libraries/windowsZones.json` file, which Date iCal uses to map
  Windows-style timezone names to real timezone IDs, is from Version24 of the
  (Unicode CLDR)[https://cldr.unicode.org/].
- This module bundles [a version of iCalcreator v2.20.x](https://github.com/joejoseph00/iCalcreator)
  with PHP 8.1 compatibility fixes by @joejoseph00 and @lkmorlan. This library
  was released under LGPL-2.1.
- The original [iCalcreator project]() is here. iCalcreator v2.22 introduced
  extensive backwards-incompatible changes to the library, so Date iCal does not
  [currently](https://github.com/backdrop-contrib/date_ical/issues/8#issuecomment-1889698040)
  support any version of iCalcreator after v2.20.x.

## License

This project is GPL v2 software. See the LICENSE.txt file in this directory for
complete text.
