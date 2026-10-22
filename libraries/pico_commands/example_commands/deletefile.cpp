/* SPDX-License-Identifier: MPL-2.0

Copyright (c) 2026 Adrian Cruceru - https://github.com/AdrianCX/picohive-firmware

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "os_fs.h"
#include "pico_logger.h"
#include "deletefile.h"
#include <string>

bool DeleteFile::on_payload(CommandContext& ctx, std::string_view body)
{
	std::string path = std::string(body);

	if (path == "config.enc")
	{
		trace("DeleteFile: file[%s] not allowed to be deleted", path.c_str());
		return ctx.reply_fmt(128, "DeleteFile: file[%s] not allowed to be deleted", path.c_str());
	}
	
	int err = os_fs_remove(path.c_str());

	if (err < 0)
	{
		trace("DeleteFile: file[%s] error[%d] failed", path.c_str(), err);
		return ctx.reply_fmt(128, "DeleteFile: file[%s] error[%d] failed", path.c_str(), err);
	}

	trace("DeleteFile: file[%s]", path.c_str());
    return ctx.reply_fmt(96, "DeleteFile: file[%s]", path.c_str());
}

MQTT_COMMAND(DeleteFile);
