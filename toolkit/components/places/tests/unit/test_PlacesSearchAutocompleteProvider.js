/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

Cu.import("resource://gre/modules/PlacesSearchAutocompleteProvider.jsm");

function run_test() {
  run_next_test();
}

add_task(function* search_engine_match() {
  let engine = yield promiseDefaultSearchEngine();
  let token = engine.getResultDomain();
  let match = yield PlacesSearchAutocompleteProvider.findMatchByToken(token.substr(0, 1));
  do_check_eq(match.url, engine.searchForm);
  do_check_eq(match.engineName, engine.name);
  do_check_eq(match.iconUrl, engine.iconURI ? engine.iconURI.spec : null);
});

add_task(function* no_match() {
  do_check_eq(null, yield PlacesSearchAutocompleteProvider.findMatchByToken("test"));
});

add_task(function* hide_search_engine_nomatch() {
  let engine = yield promiseDefaultSearchEngine();
  let token = engine.getResultDomain();
  let promiseTopic = promiseSearchTopic("engine-changed");
  Services.search.removeEngine(engine);
  yield promiseTopic;
  do_check_true(engine.hidden);
  do_check_eq(null, yield PlacesSearchAutocompleteProvider.findMatchByToken(token.substr(0, 1)));
});

add_task(function* add_search_engine_match() {
  let promiseTopic = promiseSearchTopic("engine-added");
  do_check_eq(null, yield PlacesSearchAutocompleteProvider.findMatchByToken("bacon"));
  Services.search.addEngineWithDetails("bacon", "", "bacon", "Search Bacon",
                                       "GET", "http://www.bacon.moz/?search={searchTerms}");
  yield promiseSearchTopic;
  let match = yield PlacesSearchAutocompleteProvider.findMatchByToken("bacon");
  do_check_eq(match.url, "http://www.bacon.moz");
  do_check_eq(match.engineName, "bacon");
  do_check_eq(match.iconUrl, null);
});

add_task(function* remove_search_engine_nomatch() {
  let engine = Services.search.getEngineByName("bacon");
  let promiseTopic = promiseSearchTopic("engine-removed");
  Services.search.removeEngine(engine);
  yield promiseTopic;
  do_check_eq(null, yield PlacesSearchAutocompleteProvider.findMatchByToken("bacon"));
});

add_task(function* test_parseSubmissionURL_basic() {
  // Most of the logic of parseSubmissionURL is tested in the search service
  // itself, thus we only do a sanity check of the wrapper here.
  let engine = yield promiseDefaultSearchEngine();
  let submissionURL = engine.getSubmission("terms").uri.spec;

  let result = PlacesSearchAutocompleteProvider.parseSubmissionURL(submissionURL);
  do_check_eq(result.engineName, engine.name);
  do_check_eq(result.terms, "terms");

  result = PlacesSearchAutocompleteProvider.parseSubmissionURL("http://example.org/");
  do_check_eq(result, null);
});

function promiseDefaultSearchEngine() {
  let deferred = Promise.defer();
  Services.search.init( () => {
    deferred.resolve(Services.search.defaultEngine);
  });
  return deferred.promise;
}

function promiseSearchTopic(expectedVerb) {
  let deferred = Promise.defer();
  Services.obs.addObserver( function observe(subject, topic, verb) {
    do_log_info("browser-search-engine-modified: " + verb);
    if (verb == expectedVerb) {
      Services.obs.removeObserver(observe, "browser-search-engine-modified");
      deferred.resolve();
    }
  }, "browser-search-engine-modified", false);
  return deferred.promise;
}
