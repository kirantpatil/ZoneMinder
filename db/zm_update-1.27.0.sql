--
-- This updates a 1.26.5 database to 1.27
--

--
-- Add Libvlc and cURL monitor types
--

ALTER TABLE Controls modify column Type enum('Local','Remote','Ffmpeg','Libvlc','cURL') NOT NULL default 'Local';
ALTER TABLE MonitorPresets modify column Type enum('Local','Remote','File','Ffmpeg','Libvlc','cURL') NOT NULL default 'Local';
ALTER TABLE Monitors modify column Type enum('Local','Remote','File','Ffmpeg','Libvlc','cURL') NOT NULL default 'Local';

--
-- Add required fields for cURL authenication
--
ALTER TABLE `Monitors` ADD `User` VARCHAR(32) NOT NULL AFTER `SubPath`;
ALTER TABLE `Monitors` ADD `Pass` VARCHAR(32) NOT NULL AFTER `User`;

--
-- Add default zone preset
--
INSERT INTO ZonePresets VALUES (NULL,'Default','Active','Percent','Blobs',25,NULL,3,75,3,3,3,75,2,NULL,1,NULL,0);

