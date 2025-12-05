import org.jetbrains.intellij.platform.gradle.IntelliJPlatformType.IntellijIdea
import org.jetbrains.intellij.platform.gradle.IntelliJPlatformType.IntellijIdeaCommunity
import org.jetbrains.intellij.platform.gradle.models.ProductRelease.Channel.RELEASE

plugins {
    id("java")
    id("org.jetbrains.kotlin.jvm") version "2.3.0-RC2"
    id("org.jetbrains.intellij.platform") version "2.10.5"
}

group = "com.nullptr-0"
version = "0.0.1"

repositories {
    mavenCentral()
    maven { url = uri("https://jitpack.io") }
    intellijPlatform {
        defaultRepositories()
    }
}

intellijPlatform {
    pluginConfiguration {
        ideaVersion {
            sinceBuild = "252"
        }
    }
    pluginVerification  {
        ides {
            // since 253, IntelliJ IDEA Community and Ultimate have been merged into IntelliJ IDEA
            select {
                types = listOf(IntellijIdeaCommunity)
                untilBuild = "252.*"
            }
            select {
                types = listOf(IntellijIdea)
                sinceBuild = "253"
                channels = listOf(RELEASE)
            }
        }
    }
}

dependencies {
    implementation("com.github.ballerina-platform:lsp4intellij:1ad5c72845")
    intellijPlatform {
        intellijIdea("2025.2.5")
    }
}
