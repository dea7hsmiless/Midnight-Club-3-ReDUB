package io.github.laureladastra.mcdub3;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;

class TestDiscSaveData {
    static void check(boolean ok, String name) {
        if (!ok) throw new AssertionError(name);
        System.out.println("PASS: " + name);
    }
    public static void main(String[] args) throws Exception {
        Path root=Files.createTempDirectory(new File(args[0]).toPath(), "disc-saves-");
        Path installed=Files.createDirectories(root.resolve("installed"));
        Path staging=Files.createDirectories(root.resolve("staging"));
        Path save=installed.resolve("uDaTa/54540079/profile/career.mc3");
        Files.createDirectories(save.getParent());Files.writeString(save,"career");
        Path incoming=staging.resolve("UDATA/54540079/profile/career.mc3");
        Files.createDirectories(incoming.getParent());Files.writeString(incoming,"incoming");
        Files.createDirectories(installed.resolve("TDATA"));
        Files.writeString(installed.resolve("TDATA/settings.bin"),"settings");
        DiscSaveData.preserve(installed.toFile(),staging.toFile());
        check(Files.readString(staging.resolve("uDaTa/54540079/profile/career.mc3")).equals("career"),"Android replacement preserves existing career");
        check(Files.readString(staging.resolve("TDATA/settings.bin")).equals("settings"),"Android replacement preserves title settings");
        check(Files.readString(save).equals("career"),"original remains intact before rename");
        try(var children=Files.list(staging)){check(children.count()==2,"case-duplicate save directory removed");}
        Path empty=Files.createDirectories(root.resolve("fresh"));
        DiscSaveData.preserve(root.resolve("missing").toFile(),empty.toFile());
        check(empty.toFile().list().length==0,"fresh Android install needs no save directory");
    }
}
