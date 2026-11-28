plugins {
    id("com.android.application")
}

android {
    namespace = "com.minefade.nativeengine"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.minefade.nativeengine"
        minSdk = 24
        targetSdk = 36
        versionCode = 32
        versionName = "2.4.1"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-Wall", "-Wextra", "-Wpedantic")
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
                )
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    signingConfigs {
        create("release") {
            val keyStorePath = System.getenv("MINEFADE_RELEASE_KEYSTORE_PATH")
            if (!keyStorePath.isNullOrBlank()) {
                storeFile = file(keyStorePath)
                storePassword = System.getenv("MINEFADE_RELEASE_STORE_PASSWORD")
                keyAlias = System.getenv("MINEFADE_RELEASE_KEY_ALIAS")
                keyPassword = System.getenv("MINEFADE_RELEASE_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
            isDebuggable = true
        }
        release {
            isMinifyEnabled = false
            isDebuggable = false
            signingConfig = signingConfigs.getByName("release")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            ndk { debugSymbolLevel = "SYMBOL_TABLE" }
        }
    }

    androidResources {
        // SoundPool opens these PCM assets through AssetFileDescriptor, which needs uncompressed assets.
        noCompress += listOf("wav")
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
        resources {
            excludes += setOf("META-INF/DEPENDENCIES", "META-INF/NOTICE", "META-INF/LICENSE")
        }
    }
}
