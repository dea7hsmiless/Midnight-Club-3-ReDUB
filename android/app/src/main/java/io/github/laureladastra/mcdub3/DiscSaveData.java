package io.github.laureladastra.mcdub3;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;

/** Preserve the installed profile before replacing an imported disc. */
final class DiscSaveData {
    static void preserve(File installed, File staging) throws IOException {
        if (!installed.isDirectory()) return;
        File[] children = installed.listFiles();
        if (children == null) throw new IOException("could not read existing save folders");
        for (File child : children) {
            String name = child.getName();
            if (name.equalsIgnoreCase("UDATA") || name.equalsIgnoreCase("TDATA") || name.equalsIgnoreCase("CACHE")) {
                File[] incoming = staging.listFiles();
                if (incoming == null) throw new IOException("could not read staged disc");
                for (File item : incoming)
                    if (item.getName().equalsIgnoreCase(name)) delete(item);
                copy(child, new File(staging, name));
            }
        }
    }

    private static void copy(File from, File to) throws IOException {
        if (Files.isSymbolicLink(from.toPath())) throw new IOException("save folder contains a symbolic link");
        if (from.isDirectory()) {
            if (!to.mkdirs() && !to.isDirectory()) throw new IOException("could not preserve save folder " + to);
            File[] children = from.listFiles();
            if (children == null) throw new IOException("could not read save folder " + from);
            for (File child : children) copy(child, new File(to, child.getName()));
        } else {
            Files.copy(from.toPath(), to.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private static void delete(File file) throws IOException {
        if (file.isDirectory() && !Files.isSymbolicLink(file.toPath())) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("could not read staged save folder");
            for (File child : children) delete(child);
        }
        Files.delete(file.toPath());
    }
}
