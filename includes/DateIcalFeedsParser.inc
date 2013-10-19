<?php

/**
 * @file
 *   Classes implementing Date iCal's iCalcreator-based parser functionality.
 *
 * @TODO: Figure out how to parse VVENUE information.
 */

class DateiCalFeedsParser extends FeedsParser {
  
  /**
   * Implements FeedsParser::getMappingSources().
   */
  public function getMappingSources() {
    return parent::getMappingSources() + self::getiCalMappingSources();
  }
  
  /**
   * Implements FeedsParser::parse().
   */
  public function parse(FeedsSource $source, FeedsFetcherResult $fetcher_result) {
    if (!libraries_load('iCalcreator')) {
      throw new DateIcalException(t('Unable to load the iCalcreator library. Please ensure that you have installed the library correctly.'));
    }
    
    // Read the iCal feed into memory.
    $ical_feed_contents = $fetcher_result->getRaw();
    
    // Parse the feed into an iCalcreator vcalendar object.
    $calendar = new vcalendar();
    if ($calendar->parse($ical_feed_contents) === FALSE) {
      $plugin = $source->importer->config['fetcher']['plugin_key'];
      $url = $source->config[$plugin]['source'];
      throw new DateIcalException(t('Parsing the data from %url failed. Please ensure that this URL leads to a valid iCal feed.', array('%url' => $url)));
    }
    
    // Allow modules to alter the vcalendar object before we interpret it.
    $context = array(
      'source' => $source,
      'fetcher_result' => $fetcher_result,
    );
    drupal_alter('date_ical_import_calendar', $calendar, $context);
    
    // We've got a vcalendar object created from the parsed feed data. Now we
    // need to convert that vcalendar into an array of Feeds-compatible parsed
    // data arrays. DateiCalCalendarParser does this conversion.
    $parser = new DateiCalCalendarParser($calendar, $source, $fetcher_result);
    return new FeedsParserResult($parser->parse());
  }
  
  public static function getiCalMappingSources() {
    // NOTE TO MAINTAINERS:
    // The order of these properties determines their parse order! Since we
    // need to parse the DTSTART property before parsing DTEND, the DTSTART
    // source *must* come before it in this function.
    $sources = array();
    $sources['SUMMARY'] = array(
      'name' => t('Summary/Title'),
      'description' => t('The SUMMARY property. A short summary (usually the title) of the event.'),
      'date_ical_parse_handler' => 'parseTextProperty',
    );
    $sources['DESCRIPTION'] = array(
      'name' => t('Description'),
      'description' => t('The DESCRIPTION property. A more complete description of the event than what is provided by the Summary.'),
      'date_ical_parse_handler' => 'parseTextProperty',
    );
    $sources['DTSTART'] = array(
      'name' => t('Date: Start'),
      'description' => t('The DTSTART property. The start time of each event in the feed.'),
      'date_ical_parse_handler' => 'parseDateTimeProperty',
    );
    $sources['DTEND'] = array(
      'name' => t('Date: End'),
      'description' => t('THE DTEND or DURATION property. The end time (or duration) of each event in the feed.'),
      'date_ical_parse_handler' => 'parseDateTimeProperty',
    );
    $sources['RRULE'] = array(
      'name' => t('Repeat Rule'),
      'description' => t('The RRULE property. Describes when and how often this event should repeat.
        The date field for the target node must be configured to support repeating dates, using the Date Repeat Field module (a submodule of Date).'),
      'date_ical_parse_handler' => 'parseRepeatProperty',
    );
    $sources['UID'] = array(
      'name' => 'UID',
      'description' => t('The UID property. Each event must have a UID if you wish for the import process to be able to update previously-imported nodes.
        If used, this field MUST be set to Unique.'),
      'date_ical_parse_handler' => 'parseTextProperty',
    );
    $sources['URL'] = array(
      'name' => 'URL',
      'description' => t('The URL property. Some feeds specify a URL for the event using this property.'),
      'date_ical_parse_handler' => 'parseTextProperty',
    );
    $sources['LOCATION'] = array(
      'name' => t('Location'),
      'description' => t('The LOCATION property. Can be mapped to a text field, or the title of a referenced node.'),
      'date_ical_parse_handler' => 'parseTextProperty',
    );
    $sources['LOCATION:ALTREP'] = array(
      'name' => t('Location: ALTREP'),
      'description' => t('The ALTREP value of the LOCATION property. Additional location information, usually a URL to a page with more info.'),
      'date_ical_parse_handler' => 'parsePropertyParameter',
    );
    $sources['CATEGORIES'] = array(
      'name' => t('Categories'),
      'description' => t('The CATEGORIES property. Catagories that describe the event, which can be imported into taxonomy terms.'),
      'date_ical_parse_handler' => 'parseMultivalueProperty',
    );
    return $sources;
  }
}

/**
 * Functionality to parse an iCalcreator vcalendar object into an array of
 * Feeds-compatible data arrays.
 */
class DateiCalCalendarParser {
  protected $calendar;
  protected $source;
  protected $fetcher_result;
  protected $timezones = array();
  protected $xtimezone;
  
  public function __construct($calendar, $source, $fetcher_result) {
    $this->calendar = $calendar;
    $this->source = $source;
    $this->fetcher_result = $fetcher_result;
  }
  
  /**
   * Parses the vcalendar object into an array of event data arrays.
   *
   * @return array
   *   An array keyed by the same property keys as returned by
   *   DateiCalFeedsParser::getiCalMappingSources().
   */
  public function parse() {
    // Sometimes, the feed will set a timezone for every event in the calendar
    // using the non-standard X-WR-TIMEZONE property. Date iCal uses this
    // timezone only if the date property is not in UTC and has no TZID.
    $xtimezone = $this->calendar->getProperty('X-WR-TIMEZONE');
    if (!empty($xtimezone[1])) {
      // Allow modules to alter the timezone string before it gets converted
      // into a DateTimeZone.
      $context = array(
        'property_key' => NULL,
        'calendar_component' => NULL,
        'calendar' => $this->calendar,
        'feeeds_source' => $this->source,
        'feeds_fetcher_result' => $this->fetcher_result,
      );
      drupal_alter('date_ical_import_timezone', $xtimezone[1], $context);
      $this->xtimezone = $this->_tzid_to_datetimezone($xtimezone[1]);
    }
    
    // Collect the timezones into an array, for easier access.
    while ($component = $this->calendar->getComponent('VTIMEZONE')) {
      $this->timezones[] = $component;
    }
    
    // Parse each calendar component it into a Feeds-compatible data array.
    $events = array();
    $component_types = array('VEVENT', 'VTODO', 'VJOURNAL', 'VFREEBUSY', 'VALARM');
    foreach ($component_types as $component_type) {
      while ($vcalendar_component = $this->calendar->getComponent($component_type)) {
        // Allow modules to alter the vcalendar component before we parse it
        // into a Feeds-compatible data array.
        $context = array(
          'calendar' => $this->calendar,
          'source' => $this->source,
          'fetcher_result' => $this->fetcher_result,
        );
        drupal_alter('date_ical_import_component', $vcalendar_component, $context);
        
        $parsed_component = array();
        foreach (DateiCalFeedsParser::getiCalMappingSources() as $property_key => $data) {
          $handler = $data['date_ical_parse_handler'];
          $parsed_component[$property_key] = $this->$handler($property_key, $vcalendar_component);
        }
        $events[] = $parsed_component;
      }
    }
    
    return $events;
  }
  
  /**
   * Parses text fields.
   *
   * @return string
   */
  public function parseTextProperty($property_key, $vcalendar_component) {
    $text = $vcalendar_component->getProperty($property_key);
    if ($text === FALSE) {
      if ($property_key == 'SUMMARY') {
        $uid = $vcalendar_component->getProperty('UID');
        throw new DateIcalParseException(t('The component with UID %uid is invalid because it has no SUMMARY (nodes require a title).', array('%uid' => $uid)));
      }
      // If the component doesn't have this property, return NULL.
      return NULL;
    }
    // Convert literal \n and \N into newline characters.
    $text = str_replace(array('\n', '\N'), "\n", $text);
    return $text;
  }
  
  /**
   * Parses field parameters.
   *
   * @return string
   */
  public function parsePropertyParameter($property_key, $vcalendar_component) {
    list($key, $attr) = explode(':', $property_key);
    $property = $vcalendar_component->getProperty($key, FALSE, TRUE);
    if ($property === FALSE) {
      // If the component doesn't have this property, return NULL.
      return NULL;
    }
    return isset($property['params'][$attr]) ? $property['params'][$attr]: '';
  }
  
  /**
   * Parses datetime fields.
   *
   * @return FeedsDateTime
   */
  public function parseDateTimeProperty($property_key, $vcalendar_component) {
    $property = $vcalendar_component->getProperty($property_key, FALSE, TRUE);
    // Gather all the other date properties, so we can work with them later.
    $duration = $vcalendar_component->getProperty('DURATION', FALSE, TRUE);
    $dtstart = $vcalendar_component->getProperty('DTSTART', FALSE, TRUE);
    $dtend = $vcalendar_component->getProperty('DTEND', FALSE, TRUE);
    $uid = $vcalendar_component->getProperty('UID');
    
    if ($property === FALSE) {
      if ($property_key == 'DTEND') {
        return NULL;
      }
      else if ($property_key == 'DTSTART') {
        if ($vcalendar_component->objName == 'vevent') {
          throw new DateIcalParseException(t('Feed import failed! The VEVENT with UID %uid is invalid: it has no DTSTART.', array('%uid' => $uid)));
        }
        else {
          return NULL;
        }
      }
    }
    
    // It's frustrating that iCalcreator gives us date data in a different
    // format than what it expects us to give back.
    if (isset($property['params']['TZID'])) {
      $property['value']['tz'] = $property['params']['TZID'];
    }
    
    if (isset($property['params']['VALUE']) && $property['params']['VALUE'] == 'DATE') {
      // DATE-type values are treated as All Day events, with no time-of-day.
      // They can span over multiple days.
      // The Date module's All Day event handling was never finalized:
      // http://drupal.org/node/874322
      if ($property_key == 'DTEND') {
        if ($dtstart === FALSE) {
          // This will almost certainly never happen, but the error message
          // in this case should be comprehensible.
          throw new DateIcalParseException(t('Feed import failed! The event with UID %uid is invalid: it has a DTEND but no DTSTART!', array('%uid' => $uid)));
        }
        
        // If the Date All Day module is installed, single-day All Day events
        // will be displayed wrong unless we ignore the DTEND value.
        if (module_exists('date_all_day')) {
          $prev_day = iCalUtilityFunctions::_duration2date($property['value'], array('day' => -1));
          if ($dtstart['value'] == $prev_day) {
            return NULL;
          }
          else {
            // If Date All Day is installed and this All Day event spans
            // multiple days, we need to rewind the DTEND by one day, because
            // of the problem with FeedsDateTime mentioned below.
            $property['value'] = $prev_day;
          }
        }
      }
      else if ($property_key == 'DTSTART') {
        // NOTE TO MAINTAINERS: This is why DTSTART *must* be parsed first!
        // If DTEND is parsed first, this block will have no effect.
        if ($dtend === FALSE && $duration === FALSE) {
          // If the All Day event has no DTEND and no DURATION, assume the
          // event is a single day: set DTEND = DTSTART + 1 day.
          $end = $property['value'];
          $end['day'] += 1;
          $vcalendar_component->setDtend($end['year'], $end['month'], $end['day'], FALSE, FALSE, FALSE, FALSE, array('VALUE' => 'DATE'));
        }
      }
      
      // FeedsDateTime->setTimezone() ignores timezone changes made to dates
      // with no time element, which means we can't compensate for the Date
      // module's automatic conversion to UTC when it writes to the DB. To get
      // around that, we must add 00:00:00 explicitly.
      $date_string = sprintf('%d-%d-%d 00:00:00', $property['value']['year'], $property['value']['month'], $property['value']['day']);
      // Use the server's timezone rather than letting it default to UTC.
      // This will ensure that the date value doesn't get messed up when Date
      // converts it back from UTC when it's read from the database.
      $datetimezone = new DateTimeZone(date_default_timezone_get());
    }
    else {
      // This is a DATE-TIME property.
      $date_string = iCalUtilityFunctions::_format_date_time($property['value']);
      
      if (isset($property['value']['tz'])) {
        // Z == Zulu == UTC. DateTimeZone won't acept Z, so change it to UTC.
        if (strtoupper($property['value']['tz']) == 'Z') {
          $property['value']['tz'] = 'UTC';
        }
        // Allow modules to alter the timezone string before it gets converted
        // into a DateTimeZone.
        $context = array(
          'property_key' => $property_key,
          'calendar_component' => $vcalendar_component,
          'calendar' => $this->calendar,
          'feeeds_source' => $this->source,
          'feeds_fetcher_result' => $this->fetcher_result,
        );
        drupal_alter('date_ical_import_timezone', $property['value']['tz'], $context);
        
        $datetimezone = $this->_tzid_to_datetimezone($property['value']['tz']);
      }
      else if (isset($this->xtimezone)) {
        // No timezone was set on the parsed date property, so if a timezone
        // was detected for the entire iCal feed, use it.
        $datetimezone = $this->xtimezone;
      }
      else if (count($this->timezones) == 1) {
        // There is exactly one VTIMEZONE in this feed, this date field doesn't
        // specify a timezone, and there's no X-WR-TIMEZONE. The best we can do
        // is assume this field should use the sole available TZID.
        $datetimezone = $this->_tzid_to_datetimezone($this->timezones[0]->getProperty('TZID'));
      }
      else {
        drupal_set_message(t('No timezone detected for the @key property of the event with UID %uid. Falling back to UTC.',
          array('%uid' => $uid, '@key' => $property_key)), 'warning');
        $datetimezone = new DateTimeZone('UTC');
      }
    }
    
    // NOTE TO MAINTAINERS: This is why DTSTART *must* be parsed first!
    // If DTEND is parsed first, this block will have no effect.
    if ($property_key == 'DTSTART' && $dtend === FALSE && $duration !== FALSE) {
      // In order to call $vcalendar_component->setDtend() correctly for both
      // DATE and DATE-TIME values, we need to build this dummy array first.
      $new_dtend = array(
        'year'  => NULL,
        'month' => NULL,
        'day'   => NULL,
        'hour'  => NULL,
        'min'   => NULL,
        'sec'   => NULL,
        'tz'    => NULL,
        'params' => $property['params'],
      );
      // If this component has no DTEND, but it does have a DURATION, set
      // DTEND = DTSTART + DURATION.
      $new_dtend = array_merge($new_dtend, iCalUtilityFunctions::_duration2date($property['value'], $duration['value']));
      call_user_func_array(array($vcalendar_component, 'setDtend'), $new_dtend);
    }
    
    return new FeedsDateTime($date_string, $datetimezone);
  }
  
  /**
   * Parses multi-value fields, like the CATEGORIES component.
   *
   * @return array
   *   An array of strings contaning the individual values.
   */
  public function parseMultivalueProperty($property_key, $vcalendar_component) {
    // Since we're not telling it to give us the params data, $property will
    // be either FALSE, a string, or an array of strings.
    $property = $vcalendar_component->getProperty($property_key);
    if (empty($property)) {
      // If this multi-value property is being mapped to a Taxonomy field,
      // Feeds will interpret anything besides empty array as an array of
      // empty values (e.g. array('')). This will create a term for that
      // empty value, rather than leaving the field blank.
      return array();
    }
    if (!is_array($property)) {
      $property = array($property);
    }
    return $property;
  }
  
  /**
   * Format RRULEs, which specify when and how often the event is repeated.
   *
   * @return string
   *   An RRULE string, with EXDATE and RDATE values separated by \n.
   *   This is to make the RRULE compatible with date_repeat_split_rrule().
   */
  public function parseRepeatProperty($property_key, $vcalendar_component) {
    if ($vcalendar_component->getProperty($property_key) === FALSE) {
      return NULL;
    }
    $rrule = trim($vcalendar_component->createRrule());
    $exdate = trim($vcalendar_component->createExdate());
    $rdate = trim($vcalendar_component->createRdate());
    return "$rrule\n$exdate\n$rdate";
  }
  
  protected function _tzid_to_datetimezone($tzid) {
    try {
      $datetimezone = new DateTimeZone($tzid);
    }
    catch (Exception $e) {
      $link = l('here', 'http://en.wikipedia.org/wiki/List_of_tz_database_time_zones#List', array('absolute' => TRUE));
      $msg = t(
        '"@tz" is not a valid timezone (see the TZ column !here), so Date iCal used UTC (which is probably wrong!).<br>
          Try implementing hook_date_ical_import_timezone_alter() in a custom module to fix this problem.',
        array('@tz' => $tzid, '!here' => $link)
      );
      $this->source->log('parse', $msg, array(), WATCHDOG_WARNING);
      drupal_set_message($msg, 'warning', FALSE);
      $datetimezone = new DateTimeZone("UTC");
    }
    return $datetimezone;
  }
}
