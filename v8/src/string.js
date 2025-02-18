// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var $stringCharAt;
var $stringIndexOf;
var $stringSubstring;

(function() {

%CheckIsBootstrapping();

var GlobalArray = global.Array;
var GlobalRegExp = global.RegExp;
var GlobalString = global.String;

//-------------------------------------------------------------------

function StringConstructor(x) {
  if (%_ArgumentsLength() == 0) x = '';
  if (%_IsConstructCall()) {
    %_SetValueOf(this, TO_STRING_INLINE(x));
  } else {
    return IS_SYMBOL(x) ?
        %_CallFunction(x, SymbolToString) : TO_STRING_INLINE(x);
  }
}


// ECMA-262 section 15.5.4.2
function StringToString() {
  if (!IS_STRING(this) && !IS_STRING_WRAPPER(this)) {
    throw new $TypeError('String.prototype.toString is not generic');
  }
  return %_ValueOf(this);
}


// ECMA-262 section 15.5.4.3
function StringValueOf() {
  if (!IS_STRING(this) && !IS_STRING_WRAPPER(this)) {
    throw new $TypeError('String.prototype.valueOf is not generic');
  }
  return %_ValueOf(this);
}


// ECMA-262, section 15.5.4.4
function StringCharAt(pos) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.charAt");

  var result = %_StringCharAt(this, pos);
  if (%_IsSmi(result)) {
    result = %_StringCharAt(TO_STRING_INLINE(this), TO_INTEGER(pos));
  }
  return result;
}


// ECMA-262 section 15.5.4.5
function StringCharCodeAt(pos) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.charCodeAt");

  var result = %_StringCharCodeAt(this, pos);
  if (!%_IsSmi(result)) {
    result = %_StringCharCodeAt(TO_STRING_INLINE(this), TO_INTEGER(pos));
  }
  return result;
}


// ECMA-262, section 15.5.4.6
function StringConcat(other /* and more */) {  // length == 1
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.concat");

  var len = %_ArgumentsLength();
  var this_as_string = TO_STRING_INLINE(this);
  if (len === 1) {
    return this_as_string + other;
  }
  var parts = new InternalArray(len + 1);
  parts[0] = this_as_string;
  for (var i = 0; i < len; i++) {
    var part = %_Arguments(i);
    parts[i + 1] = TO_STRING_INLINE(part);
  }
  return %StringBuilderConcat(parts, len + 1, "");
}


// ECMA-262 section 15.5.4.7
function StringIndexOfJS(pattern /* position */) {  // length == 1
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.indexOf");

  var subject = TO_STRING_INLINE(this);
  pattern = TO_STRING_INLINE(pattern);
  var index = 0;
  if (%_ArgumentsLength() > 1) {
    index = %_Arguments(1);  // position
    index = TO_INTEGER(index);
    if (index < 0) index = 0;
    if (index > subject.length) index = subject.length;
  }
  return %StringIndexOf(subject, pattern, index);
}


// ECMA-262 section 15.5.4.8
function StringLastIndexOfJS(pat /* position */) {  // length == 1
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.lastIndexOf");

  var sub = TO_STRING_INLINE(this);
  var subLength = sub.length;
  var pat = TO_STRING_INLINE(pat);
  var patLength = pat.length;
  var index = subLength - patLength;
  if (%_ArgumentsLength() > 1) {
    var position = ToNumber(%_Arguments(1));
    if (!NUMBER_IS_NAN(position)) {
      position = TO_INTEGER(position);
      if (position < 0) {
        position = 0;
      }
      if (position + patLength < subLength) {
        index = position;
      }
    }
  }
  if (index < 0) {
    return -1;
  }
  return %StringLastIndexOf(sub, pat, index);
}


// ECMA-262 section 15.5.4.9
//
// This function is implementation specific.  For now, we do not
// do anything locale specific.
function StringLocaleCompareJS(other) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.localeCompare");

  return %StringLocaleCompare(TO_STRING_INLINE(this),
                              TO_STRING_INLINE(other));
}


// ECMA-262 section 15.5.4.10
function StringMatchJS(regexp) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.match");

  var subject = TO_STRING_INLINE(this);
  if (IS_REGEXP(regexp)) {
    // Emulate RegExp.prototype.exec's side effect in step 5, even though
    // value is discarded.
    var lastIndex = regexp.lastIndex;
    TO_INTEGER_FOR_SIDE_EFFECT(lastIndex);
    if (!regexp.global) return $regexpExecNoTests(regexp, subject, 0);
    var result = %StringMatch(subject, regexp, $regexpLastMatchInfo);
    if (result !== null) $regexpLastMatchInfoOverride = null;
    regexp.lastIndex = 0;
    return result;
  }
  // Non-regexp argument.
  regexp = new GlobalRegExp(regexp);
  return $regexpExecNoTests(regexp, subject, 0);
}


var NORMALIZATION_FORMS = ['NFC', 'NFD', 'NFKC', 'NFKD'];


// ECMA-262 v6, section 21.1.3.12
//
// For now we do nothing, as proper normalization requires big tables.
// If Intl is enabled, then i18n.js will override it and provide the the
// proper functionality.
function StringNormalizeJS(form) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.normalize");

  var form = form ? TO_STRING_INLINE(form) : 'NFC';
  var normalizationForm = NORMALIZATION_FORMS.indexOf(form);
  if (normalizationForm === -1) {
    throw new $RangeError('The normalization form should be one of '
        + NORMALIZATION_FORMS.join(', ') + '.');
  }

  return %_ValueOf(this);
}


// This has the same size as the $regexpLastMatchInfo array, and can be used
// for functions that expect that structure to be returned.  It is used when
// the needle is a string rather than a regexp.  In this case we can't update
// lastMatchArray without erroneously affecting the properties on the global
// RegExp object.
var reusableMatchInfo = [2, "", "", -1, -1];


// ECMA-262, section 15.5.4.11
function StringReplace(search, replace) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.replace");

  var subject = TO_STRING_INLINE(this);

  // Decision tree for dispatch
  // .. regexp search
  // .... string replace
  // ...... non-global search
  // ........ empty string replace
  // ........ non-empty string replace (with $-expansion)
  // ...... global search
  // ........ no need to circumvent last match info override
  // ........ need to circument last match info override
  // .... function replace
  // ...... global search
  // ...... non-global search
  // .. string search
  // .... special case that replaces with one single character
  // ...... function replace
  // ...... string replace (with $-expansion)

  if (IS_REGEXP(search)) {
    // Emulate RegExp.prototype.exec's side effect in step 5, even if
    // value is discarded.
    var lastIndex = search.lastIndex;
    TO_INTEGER_FOR_SIDE_EFFECT(lastIndex);

    if (!IS_SPEC_FUNCTION(replace)) {
      replace = TO_STRING_INLINE(replace);

      if (!search.global) {
        // Non-global regexp search, string replace.
        var match = $regexpExec(search, subject, 0);
        if (match == null) {
          search.lastIndex = 0
          return subject;
        }
        if (replace.length == 0) {
          return %_SubString(subject, 0, match[CAPTURE0]) +
                 %_SubString(subject, match[CAPTURE1], subject.length)
        }
        return ExpandReplacement(replace, subject, $regexpLastMatchInfo,
                                 %_SubString(subject, 0, match[CAPTURE0])) +
               %_SubString(subject, match[CAPTURE1], subject.length);
      }

      // Global regexp search, string replace.
      search.lastIndex = 0;
      if ($regexpLastMatchInfoOverride == null) {
        return %StringReplaceGlobalRegExpWithString(
            subject, search, replace, $regexpLastMatchInfo);
      } else {
        // We use this hack to detect whether StringReplaceRegExpWithString
        // found at least one hit. In that case we need to remove any
        // override.
        var saved_subject = $regexpLastMatchInfo[LAST_SUBJECT_INDEX];
        $regexpLastMatchInfo[LAST_SUBJECT_INDEX] = 0;
        var answer = %StringReplaceGlobalRegExpWithString(
            subject, search, replace, $regexpLastMatchInfo);
        if (%_IsSmi($regexpLastMatchInfo[LAST_SUBJECT_INDEX])) {
          $regexpLastMatchInfo[LAST_SUBJECT_INDEX] = saved_subject;
        } else {
          $regexpLastMatchInfoOverride = null;
        }
        return answer;
      }
    }

    if (search.global) {
      // Global regexp search, function replace.
      return StringReplaceGlobalRegExpWithFunction(subject, search, replace);
    }
    // Non-global regexp search, function replace.
    return StringReplaceNonGlobalRegExpWithFunction(subject, search, replace);
  }

  search = TO_STRING_INLINE(search);

  if (search.length == 1 &&
      subject.length > 0xFF &&
      IS_STRING(replace) &&
      %StringIndexOf(replace, '$', 0) < 0) {
    // Searching by traversing a cons string tree and replace with cons of
    // slices works only when the replaced string is a single character, being
    // replaced by a simple string and only pays off for long strings.
    return %StringReplaceOneCharWithString(subject, search, replace);
  }
  var start = %StringIndexOf(subject, search, 0);
  if (start < 0) return subject;
  var end = start + search.length;

  var result = %_SubString(subject, 0, start);

  // Compute the string to replace with.
  if (IS_SPEC_FUNCTION(replace)) {
    var receiver = %GetDefaultReceiver(replace);
    result += %_CallFunction(receiver, search, start, subject, replace);
  } else {
    reusableMatchInfo[CAPTURE0] = start;
    reusableMatchInfo[CAPTURE1] = end;
    result = ExpandReplacement(TO_STRING_INLINE(replace),
                               subject,
                               reusableMatchInfo,
                               result);
  }

  return result + %_SubString(subject, end, subject.length);
}


// Expand the $-expressions in the string and return a new string with
// the result.
function ExpandReplacement(string, subject, matchInfo, result) {
  var length = string.length;
  var next = %StringIndexOf(string, '$', 0);
  if (next < 0) {
    if (length > 0) result += string;
    return result;
  }

  if (next > 0) result += %_SubString(string, 0, next);

  while (true) {
    var expansion = '$';
    var position = next + 1;
    if (position < length) {
      var peek = %_StringCharCodeAt(string, position);
      if (peek == 36) {         // $$
        ++position;
        result += '$';
      } else if (peek == 38) {  // $& - match
        ++position;
        result +=
          %_SubString(subject, matchInfo[CAPTURE0], matchInfo[CAPTURE1]);
      } else if (peek == 96) {  // $` - prefix
        ++position;
        result += %_SubString(subject, 0, matchInfo[CAPTURE0]);
      } else if (peek == 39) {  // $' - suffix
        ++position;
        result += %_SubString(subject, matchInfo[CAPTURE1], subject.length);
      } else if (peek >= 48 && peek <= 57) {
        // Valid indices are $1 .. $9, $01 .. $09 and $10 .. $99
        var scaled_index = (peek - 48) << 1;
        var advance = 1;
        var number_of_captures = NUMBER_OF_CAPTURES(matchInfo);
        if (position + 1 < string.length) {
          var next = %_StringCharCodeAt(string, position + 1);
          if (next >= 48 && next <= 57) {
            var new_scaled_index = scaled_index * 10 + ((next - 48) << 1);
            if (new_scaled_index < number_of_captures) {
              scaled_index = new_scaled_index;
              advance = 2;
            }
          }
        }
        if (scaled_index != 0 && scaled_index < number_of_captures) {
          var start = matchInfo[CAPTURE(scaled_index)];
          if (start >= 0) {
            result +=
              %_SubString(subject, start, matchInfo[CAPTURE(scaled_index + 1)]);
          }
          position += advance;
        } else {
          result += '$';
        }
      } else {
        result += '$';
      }
    } else {
      result += '$';
    }

    // Go the the next $ in the string.
    next = %StringIndexOf(string, '$', position);

    // Return if there are no more $ characters in the string. If we
    // haven't reached the end, we need to append the suffix.
    if (next < 0) {
      if (position < length) {
        result += %_SubString(string, position, length);
      }
      return result;
    }

    // Append substring between the previous and the next $ character.
    if (next > position) {
      result += %_SubString(string, position, next);
    }
  }
  return result;
}


// Compute the string of a given regular expression capture.
function CaptureString(string, lastCaptureInfo, index) {
  // Scale the index.
  var scaled = index << 1;
  // Compute start and end.
  var start = lastCaptureInfo[CAPTURE(scaled)];
  // If start isn't valid, return undefined.
  if (start < 0) return;
  var end = lastCaptureInfo[CAPTURE(scaled + 1)];
  return %_SubString(string, start, end);
}


// TODO(lrn): This array will survive indefinitely if replace is never
// called again. However, it will be empty, since the contents are cleared
// in the finally block.
var reusableReplaceArray = new InternalArray(16);

// Helper function for replacing regular expressions with the result of a
// function application in String.prototype.replace.
function StringReplaceGlobalRegExpWithFunction(subject, regexp, replace) {
  var resultArray = reusableReplaceArray;
  if (resultArray) {
    reusableReplaceArray = null;
  } else {
    // Inside a nested replace (replace called from the replacement function
    // of another replace) or we have failed to set the reusable array
    // back due to an exception in a replacement function. Create a new
    // array to use in the future, or until the original is written back.
    resultArray = new InternalArray(16);
  }
  var res = %RegExpExecMultiple(regexp,
                                subject,
                                $regexpLastMatchInfo,
                                resultArray);
  regexp.lastIndex = 0;
  if (IS_NULL(res)) {
    // No matches at all.
    reusableReplaceArray = resultArray;
    return subject;
  }
  var len = res.length;
  if (NUMBER_OF_CAPTURES($regexpLastMatchInfo) == 2) {
    // If the number of captures is two then there are no explicit captures in
    // the regexp, just the implicit capture that captures the whole match.  In
    // this case we can simplify quite a bit and end up with something faster.
    // The builder will consist of some integers that indicate slices of the
    // input string and some replacements that were returned from the replace
    // function.
    var match_start = 0;
    var override = new InternalPackedArray(null, 0, subject);
    var receiver = %GetDefaultReceiver(replace);
    for (var i = 0; i < len; i++) {
      var elem = res[i];
      if (%_IsSmi(elem)) {
        // Integers represent slices of the original string.  Use these to
        // get the offsets we need for the override array (so things like
        // RegExp.leftContext work during the callback function.
        if (elem > 0) {
          match_start = (elem >> 11) + (elem & 0x7ff);
        } else {
          match_start = res[++i] - elem;
        }
      } else {
        override[0] = elem;
        override[1] = match_start;
        $regexpLastMatchInfoOverride = override;
        var func_result =
            %_CallFunction(receiver, elem, match_start, subject, replace);
        // Overwrite the i'th element in the results with the string we got
        // back from the callback function.
        res[i] = TO_STRING_INLINE(func_result);
        match_start += elem.length;
      }
    }
  } else {
    var receiver = %GetDefaultReceiver(replace);
    for (var i = 0; i < len; i++) {
      var elem = res[i];
      if (!%_IsSmi(elem)) {
        // elem must be an Array.
        // Use the apply argument as backing for global RegExp properties.
        $regexpLastMatchInfoOverride = elem;
        var func_result = %Apply(replace, receiver, elem, 0, elem.length);
        // Overwrite the i'th element in the results with the string we got
        // back from the callback function.
        res[i] = TO_STRING_INLINE(func_result);
      }
    }
  }
  var result = %StringBuilderConcat(res, res.length, subject);
  resultArray.length = 0;
  reusableReplaceArray = resultArray;
  return result;
}


function StringReplaceNonGlobalRegExpWithFunction(subject, regexp, replace) {
  var matchInfo = $regexpExec(regexp, subject, 0);
  if (IS_NULL(matchInfo)) {
    regexp.lastIndex = 0;
    return subject;
  }
  var index = matchInfo[CAPTURE0];
  var result = %_SubString(subject, 0, index);
  var endOfMatch = matchInfo[CAPTURE1];
  // Compute the parameter list consisting of the match, captures, index,
  // and subject for the replace function invocation.
  // The number of captures plus one for the match.
  var m = NUMBER_OF_CAPTURES(matchInfo) >> 1;
  var replacement;
  var receiver = %GetDefaultReceiver(replace);
  if (m == 1) {
    // No captures, only the match, which is always valid.
    var s = %_SubString(subject, index, endOfMatch);
    // Don't call directly to avoid exposing the built-in global object.
    replacement = %_CallFunction(receiver, s, index, subject, replace);
  } else {
    var parameters = new InternalArray(m + 2);
    for (var j = 0; j < m; j++) {
      parameters[j] = CaptureString(subject, matchInfo, j);
    }
    parameters[j] = index;
    parameters[j + 1] = subject;

    replacement = %Apply(replace, receiver, parameters, 0, j + 2);
  }

  result += replacement;  // The add method converts to string if necessary.
  // Can't use matchInfo any more from here, since the function could
  // overwrite it.
  return result + %_SubString(subject, endOfMatch, subject.length);
}


// ECMA-262 section 15.5.4.12
function StringSearch(re) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.search");

  var regexp;
  if (IS_STRING(re)) {
    regexp = %_GetFromCache(STRING_TO_REGEXP_CACHE_ID, re);
  } else if (IS_REGEXP(re)) {
    regexp = re;
  } else {
    regexp = new GlobalRegExp(re);
  }
  var match = $regexpExec(regexp, TO_STRING_INLINE(this), 0);
  if (match) {
    return match[CAPTURE0];
  }
  return -1;
}


// ECMA-262 section 15.5.4.13
function StringSlice(start, end) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.slice");

  var s = TO_STRING_INLINE(this);
  var s_len = s.length;
  var start_i = TO_INTEGER(start);
  var end_i = s_len;
  if (!IS_UNDEFINED(end)) {
    end_i = TO_INTEGER(end);
  }

  if (start_i < 0) {
    start_i += s_len;
    if (start_i < 0) {
      start_i = 0;
    }
  } else {
    if (start_i > s_len) {
      return '';
    }
  }

  if (end_i < 0) {
    end_i += s_len;
    if (end_i < 0) {
      return '';
    }
  } else {
    if (end_i > s_len) {
      end_i = s_len;
    }
  }

  if (end_i <= start_i) {
    return '';
  }

  return %_SubString(s, start_i, end_i);
}


// ECMA-262 section 15.5.4.14
function StringSplitJS(separator, limit) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.split");

  var subject = TO_STRING_INLINE(this);
  limit = (IS_UNDEFINED(limit)) ? 0xffffffff : TO_UINT32(limit);

  var length = subject.length;
  if (!IS_REGEXP(separator)) {
    var separator_string = TO_STRING_INLINE(separator);

    if (limit === 0) return [];

    // ECMA-262 says that if separator is undefined, the result should
    // be an array of size 1 containing the entire string.
    if (IS_UNDEFINED(separator)) return [subject];

    var separator_length = separator_string.length;

    // If the separator string is empty then return the elements in the subject.
    if (separator_length === 0) return %StringToArray(subject, limit);

    var result = %StringSplit(subject, separator_string, limit);

    return result;
  }

  if (limit === 0) return [];

  // Separator is a regular expression.
  return StringSplitOnRegExp(subject, separator, limit, length);
}


function StringSplitOnRegExp(subject, separator, limit, length) {
  if (length === 0) {
    if ($regexpExec(separator, subject, 0, 0) != null) {
      return [];
    }
    return [subject];
  }

  var currentIndex = 0;
  var startIndex = 0;
  var startMatch = 0;
  var result = new InternalArray();

  outer_loop:
  while (true) {

    if (startIndex === length) {
      result[result.length] = %_SubString(subject, currentIndex, length);
      break;
    }

    var matchInfo = $regexpExec(separator, subject, startIndex);
    if (matchInfo == null || length === (startMatch = matchInfo[CAPTURE0])) {
      result[result.length] = %_SubString(subject, currentIndex, length);
      break;
    }
    var endIndex = matchInfo[CAPTURE1];

    // We ignore a zero-length match at the currentIndex.
    if (startIndex === endIndex && endIndex === currentIndex) {
      startIndex++;
      continue;
    }

    result[result.length] = %_SubString(subject, currentIndex, startMatch);

    if (result.length === limit) break;

    var matchinfo_len = NUMBER_OF_CAPTURES(matchInfo) + REGEXP_FIRST_CAPTURE;
    for (var i = REGEXP_FIRST_CAPTURE + 2; i < matchinfo_len; ) {
      var start = matchInfo[i++];
      var end = matchInfo[i++];
      if (end != -1) {
        result[result.length] = %_SubString(subject, start, end);
      } else {
        result[result.length] = UNDEFINED;
      }
      if (result.length === limit) break outer_loop;
    }

    startIndex = currentIndex = endIndex;
  }
  var array_result = [];
  %MoveArrayContents(result, array_result);
  return array_result;
}


// ECMA-262 section 15.5.4.15
function StringSubstring(start, end) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.subString");

  var s = TO_STRING_INLINE(this);
  var s_len = s.length;

  var start_i = TO_INTEGER(start);
  if (start_i < 0) {
    start_i = 0;
  } else if (start_i > s_len) {
    start_i = s_len;
  }

  var end_i = s_len;
  if (!IS_UNDEFINED(end)) {
    end_i = TO_INTEGER(end);
    if (end_i > s_len) {
      end_i = s_len;
    } else {
      if (end_i < 0) end_i = 0;
      if (start_i > end_i) {
        var tmp = end_i;
        end_i = start_i;
        start_i = tmp;
      }
    }
  }

  return %_SubString(s, start_i, end_i);
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.1
function StringSubstr(start, n) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.substr");

  var s = TO_STRING_INLINE(this);
  var len;

  // Correct n: If not given, set to string length; if explicitly
  // set to undefined, zero, or negative, returns empty string.
  if (IS_UNDEFINED(n)) {
    len = s.length;
  } else {
    len = TO_INTEGER(n);
    if (len <= 0) return '';
  }

  // Correct start: If not given (or undefined), set to zero; otherwise
  // convert to integer and handle negative case.
  if (IS_UNDEFINED(start)) {
    start = 0;
  } else {
    start = TO_INTEGER(start);
    // If positive, and greater than or equal to the string length,
    // return empty string.
    if (start >= s.length) return '';
    // If negative and absolute value is larger than the string length,
    // use zero.
    if (start < 0) {
      start += s.length;
      if (start < 0) start = 0;
    }
  }

  var end = start + len;
  if (end > s.length) end = s.length;

  return %_SubString(s, start, end);
}


// ECMA-262, 15.5.4.16
function StringToLowerCaseJS() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.toLowerCase");

  return %StringToLowerCase(TO_STRING_INLINE(this));
}


// ECMA-262, 15.5.4.17
function StringToLocaleLowerCase() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.toLocaleLowerCase");

  return %StringToLowerCase(TO_STRING_INLINE(this));
}


// ECMA-262, 15.5.4.18
function StringToUpperCaseJS() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.toUpperCase");

  return %StringToUpperCase(TO_STRING_INLINE(this));
}


// ECMA-262, 15.5.4.19
function StringToLocaleUpperCase() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.toLocaleUpperCase");

  return %StringToUpperCase(TO_STRING_INLINE(this));
}

// ES5, 15.5.4.20
function StringTrimJS() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.trim");

  return %StringTrim(TO_STRING_INLINE(this), true, true);
}

function StringTrimLeft() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.trimLeft");

  return %StringTrim(TO_STRING_INLINE(this), true, false);
}

function StringTrimRight() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.trimRight");

  return %StringTrim(TO_STRING_INLINE(this), false, true);
}


// ECMA-262, section 15.5.3.2
function StringFromCharCode(code) {
  var n = %_ArgumentsLength();
  if (n == 1) {
    if (!%_IsSmi(code)) code = ToNumber(code);
    return %_StringCharFromCode(code & 0xffff);
  }

  var one_byte = %NewString(n, NEW_ONE_BYTE_STRING);
  var i;
  for (i = 0; i < n; i++) {
    var code = %_Arguments(i);
    if (!%_IsSmi(code)) code = ToNumber(code) & 0xffff;
    if (code < 0) code = code & 0xffff;
    if (code > 0xff) break;
    %_OneByteSeqStringSetChar(i, code, one_byte);
  }
  if (i == n) return one_byte;
  one_byte = %TruncateString(one_byte, i);

  var two_byte = %NewString(n - i, NEW_TWO_BYTE_STRING);
  for (var j = 0; i < n; i++, j++) {
    var code = %_Arguments(i);
    if (!%_IsSmi(code)) code = ToNumber(code) & 0xffff;
    %_TwoByteSeqStringSetChar(j, code, two_byte);
  }
  return one_byte + two_byte;
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.2.1
function HtmlEscape(str) {
  return TO_STRING_INLINE(str).replace(/"/g, "&quot;");
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.2
function StringAnchor(name) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.anchor");
  return "<a name=\"" + HtmlEscape(name) + "\">" + this + "</a>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.3
function StringBig() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.big");
  return "<big>" + this + "</big>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.4
function StringBlink() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.blink");
  return "<blink>" + this + "</blink>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.5
function StringBold() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.bold");
  return "<b>" + this + "</b>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.6
function StringFixed() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.fixed");
  return "<tt>" + this + "</tt>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.7
function StringFontcolor(color) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.fontcolor");
  return "<font color=\"" + HtmlEscape(color) + "\">" + this + "</font>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.8
function StringFontsize(size) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.fontsize");
  return "<font size=\"" + HtmlEscape(size) + "\">" + this + "</font>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.9
function StringItalics() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.italics");
  return "<i>" + this + "</i>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.10
function StringLink(s) {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.link");
  return "<a href=\"" + HtmlEscape(s) + "\">" + this + "</a>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.11
function StringSmall() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.small");
  return "<small>" + this + "</small>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.12
function StringStrike() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.strike");
  return "<strike>" + this + "</strike>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.13
function StringSub() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.sub");
  return "<sub>" + this + "</sub>";
}


// ES6 draft, revision 26 (2014-07-18), section B.2.3.14
function StringSup() {
  CHECK_OBJECT_COERCIBLE(this, "String.prototype.sup");
  return "<sup>" + this + "</sup>";
}

// -------------------------------------------------------------------

// Set the String function and constructor.
%SetCode(GlobalString, StringConstructor);
%FunctionSetPrototype(GlobalString, new GlobalString());

// Set up the constructor property on the String prototype object.
%AddNamedProperty(
    GlobalString.prototype, "constructor", GlobalString, DONT_ENUM);

// Set up the non-enumerable functions on the String object.
InstallFunctions(GlobalString, DONT_ENUM, GlobalArray(
  "fromCharCode", StringFromCharCode
));

// Set up the non-enumerable functions on the String prototype object.
InstallFunctions(GlobalString.prototype, DONT_ENUM, GlobalArray(
  "valueOf", StringValueOf,
  "toString", StringToString,
  "charAt", StringCharAt,
  "charCodeAt", StringCharCodeAt,
  "concat", StringConcat,
  "indexOf", StringIndexOfJS,
  "lastIndexOf", StringLastIndexOfJS,
  "localeCompare", StringLocaleCompareJS,
  "match", StringMatchJS,
  "normalize", StringNormalizeJS,
  "replace", StringReplace,
  "search", StringSearch,
  "slice", StringSlice,
  "split", StringSplitJS,
  "substring", StringSubstring,
  "substr", StringSubstr,
  "toLowerCase", StringToLowerCaseJS,
  "toLocaleLowerCase", StringToLocaleLowerCase,
  "toUpperCase", StringToUpperCaseJS,
  "toLocaleUpperCase", StringToLocaleUpperCase,
  "trim", StringTrimJS,
  "trimLeft", StringTrimLeft,
  "trimRight", StringTrimRight,
  "link", StringLink,
  "anchor", StringAnchor,
  "fontcolor", StringFontcolor,
  "fontsize", StringFontsize,
  "big", StringBig,
  "blink", StringBlink,
  "bold", StringBold,
  "fixed", StringFixed,
  "italics", StringItalics,
  "small", StringSmall,
  "strike", StringStrike,
  "sub", StringSub,
  "sup", StringSup
));

$stringCharAt = StringCharAt;
$stringIndexOf = StringIndexOfJS;
$stringSubstring = StringSubstring;

})();
