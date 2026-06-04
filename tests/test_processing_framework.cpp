// Processing Framework tests — verify cache, progress callback, error reporting
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>

#include <processing/framework/processing_cache.h>
#include <processing/framework/progress_callback.h>
#include <processing/framework/error_reporter.h>

using namespace sicnu;

// ==================== TESTS ====================

TEST_CASE("ProcessingCache basic operations", "[processing][cache]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    ProcessingCache cache(tempDir.path());

    SECTION("Cache starts empty") {
        CHECK(cache.size() == 0);
    }

    SECTION("Store and retrieve") {
        QByteArray data = "Hello, World!";
        CHECK(cache.store("test_key", data) == true);
        CHECK(cache.contains("test_key") == true);
        CHECK(cache.retrieve("test_key") == data);
    }

    SECTION("Retrieve non-existent key") {
        CHECK(cache.contains("nonexistent") == false);
        CHECK(cache.retrieve("nonexistent").isEmpty());
    }

    SECTION("Remove entry") {
        cache.store("to_remove", "data");
        CHECK(cache.contains("to_remove") == true);

        CHECK(cache.remove("to_remove") == true);
        CHECK(cache.contains("to_remove") == false);
    }

    SECTION("Clear all entries") {
        cache.store("key1", "data1");
        cache.store("key2", "data2");
        CHECK(cache.size() == 2);

        cache.clear();
        CHECK(cache.size() == 0);
    }
}

TEST_CASE("ProcessingCache large data", "[processing][cache]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    ProcessingCache cache(tempDir.path());

    SECTION("Store large data") {
        QByteArray largeData(1024 * 1024, 'X'); // 1MB
        CHECK(cache.store("large", largeData) == true);
        CHECK(cache.retrieve("large") == largeData);
    }
}

TEST_CASE("ProcessingCache store returns false on write failure", "[processing][cache]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    // Create a file where the cache subdirectory should be — forces write failure
    QString blockingPath = tempDir.path() + "/blocked";
    QFile blocker(blockingPath);
    REQUIRE(blocker.open(QIODevice::WriteOnly));
    blocker.write("x");
    blocker.close();

    // Use a path INSIDE the blocking file as cache dir — open will fail
    ProcessingCache cache(blockingPath + "/subdir");

    CHECK(cache.store("key", "data") == false);
}

TEST_CASE("ProcessingCache store verifies data integrity", "[processing][cache]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    ProcessingCache cache(tempDir.path());

    // After store, retrieve should return exact same data
    QByteArray data = "Test data for integrity check";
    bool stored = cache.store("integrity_key", data);
    REQUIRE(stored == true);

    QByteArray retrieved = cache.retrieve("integrity_key");
    CHECK(retrieved == data);
    CHECK(retrieved.size() == data.size());
}

TEST_CASE("ProcessingCache max size setting", "[processing][cache]") {
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());

    ProcessingCache cache(tempDir.path());

    SECTION("Default max size is 100MB") {
        CHECK(cache.maxSizeBytes() == 1024 * 1024 * 100);
    }

    SECTION("Can set max size") {
        cache.setMaxSizeBytes(1024 * 1024 * 50); // 50MB
        CHECK(cache.maxSizeBytes() == 1024 * 1024 * 50);
    }
}

TEST_CASE("SimpleProgressCallback lifecycle", "[processing][progress]") {
    SimpleProgressCallback callback;

    SECTION("Initial state") {
        CHECK(callback.isStarted() == false);
        CHECK(callback.isCompleted() == false);
        CHECK(callback.isCancelled() == false);
    }

    SECTION("onStart sets task info") {
        callback.onStart("Test Task", 10);
        CHECK(callback.isStarted() == true);
        CHECK(callback.taskName() == "Test Task");
        CHECK(callback.totalSteps() == 10);
    }

    SECTION("onProgress updates current step") {
        callback.onStart("Task", 5);
        callback.onProgress(3, "Processing...");
        CHECK(callback.currentStep() == 3);
        CHECK(callback.lastMessage() == "Processing...");
    }

    SECTION("onComplete marks as finished") {
        callback.onStart("Task", 1);
        callback.onComplete(true, "Done!");
        CHECK(callback.isCompleted() == true);
        CHECK(callback.isSuccess() == true);
        CHECK(callback.lastMessage() == "Done!");
    }

    SECTION("onComplete with failure") {
        callback.onStart("Task", 1);
        callback.onComplete(false, "Failed!");
        CHECK(callback.isCompleted() == true);
        CHECK(callback.isSuccess() == false);
    }

    SECTION("Cancel sets flag") {
        CHECK(callback.isCancelled() == false);
        callback.cancel();
        CHECK(callback.isCancelled() == true);
    }
}

TEST_CASE("ErrorReporter basic operations", "[processing][errors]") {
    ErrorReporter reporter;

    SECTION("Starts with no errors") {
        CHECK(reporter.hasErrors() == false);
        CHECK(reporter.errorCount() == 0);
    }

    SECTION("Report error") {
        reporter.reportError("gdal", "gdal_translate", "File not found", -1);
        CHECK(reporter.hasErrors() == true);
        CHECK(reporter.errorCount() == 1);
    }

    SECTION("Last error") {
        reporter.reportError("gdal", "gdalwarp", "Invalid CRS", 1);
        reporter.reportError("otb", "BandMath", "Expression error", 2);

        ProcessingError last = reporter.lastError();
        CHECK(last.provider == "otb");
        CHECK(last.algorithm == "BandMath");
        CHECK(last.message == "Expression error");
        CHECK(last.errorCode == 2);
    }

    SECTION("Clear errors") {
        reporter.reportError("gdal", "test", "error", 0);
        reporter.clear();
        CHECK(reporter.hasErrors() == false);
        CHECK(reporter.errorCount() == 0);
    }

    SECTION("Error has timestamp") {
        QDateTime before = QDateTime::currentDateTime();
        reporter.reportError("gdal", "test", "error", 0);
        QDateTime after = QDateTime::currentDateTime();

        ProcessingError error = reporter.lastError();
        CHECK(error.timestamp >= before);
        CHECK(error.timestamp <= after);
    }
}

TEST_CASE("ErrorReporter multiple errors", "[processing][errors]") {
    ErrorReporter reporter;

    SECTION("Tracks multiple errors") {
        reporter.reportError("gdal", "alg1", "error1", 1);
        reporter.reportError("gdal", "alg2", "error2", 2);
        reporter.reportError("otb", "alg3", "error3", 3);

        CHECK(reporter.errorCount() == 3);
        CHECK(reporter.errors().size() == 3);
    }
}
