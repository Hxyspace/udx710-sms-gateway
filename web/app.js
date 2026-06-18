const baseUrl = "/";

layui.use(function () {
  var element = layui.element;
  var table = layui.table;
  var layer = layui.layer;
  var form = layui.form;

  var state = {
    in: { page: 1, limit: 10, count: 0 },
    out: { page: 1, limit: 10, count: 0 }
  };

  function escapeHtml(value) {
    return String(value || "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function formatTime(timestamp) {
    return timestamp ? new Date(timestamp * 1000).toLocaleString() : "-";
  }

  function totalPages(direction) {
    return Math.max(1, Math.ceil(state[direction].count / state[direction].limit));
  }

  function tableId(direction) {
    return direction === "in" ? "inTable" : "outTable";
  }

  function phoneTitle(direction) {
    return direction === "in" ? "发件人" : "收件人";
  }

  function updateInfo(direction) {
    var total = totalPages(direction);
    document.getElementById(direction + "Count").innerText = "条数: " + state[direction].count;
    document.getElementById(direction + "Pages").innerText = "页数: " + total;
    document.getElementById(direction + "Limit").innerText = "每页: " + state[direction].limit;

    var pageSelect = document.getElementById(direction + "PageSelect");
    pageSelect.innerHTML = "";
    for (var i = 1; i <= total; i++) {
      var option = document.createElement("option");
      option.value = i;
      option.textContent = i;
      pageSelect.appendChild(option);
    }
    pageSelect.value = state[direction].page;

    document.getElementById(direction + "PrevPage").disabled = state[direction].page <= 1;
    document.getElementById(direction + "NextPage").disabled = state[direction].page >= total;
    form.render("select");
  }

  function renderTable(direction, rows) {
    table.render({
      elem: "#" + tableId(direction),
      id: tableId(direction),
      data: rows,
      page: false,
      limit: state[direction].limit,
      text: { none: "暂无短信" },
      cols: [[
        { type: "checkbox", fixed: "left" },
        {
          field: "content",
          title: "内容",
          minWidth: 260,
          templet: function (d) { return escapeHtml(d.content); }
        },
        {
          field: "phone",
          title: phoneTitle(direction),
          minWidth: 150,
          templet: function (d) { return escapeHtml(d.phone); }
        },
        {
          field: "timestamp",
          title: "时间",
          minWidth: 180,
          templet: function (d) { return formatTime(d.timestamp); }
        }
      ]]
    });
  }

  function fetchMessages(direction) {
    var current = state[direction];
    return fetch(baseUrl + "api/messages?direction=" + encodeURIComponent(direction) +
      "&page=" + current.page +
      "&page_size=" + current.limit)
      .then(function (response) { return response.json(); })
      .then(function (data) {
        current.count = data.total || 0;
        current.page = data.page || current.page;
        current.limit = data.page_size || current.limit;
        renderTable(direction, data.items || []);
        updateInfo(direction);
      })
      .catch(function () {
        layer.msg("短信加载失败");
      });
  }

  function openMessage(data, direction) {
    layer.open({
      type: 1,
      title: direction === "in" ? "短信详情" : "发送详情",
      shadeClose: true,
      area: [window.innerWidth < 640 ? "92%" : "560px", "auto"],
      content:
        "<div class='detail-dialog'>" +
        "<p><strong>" + phoneTitle(direction) + "：</strong>" + escapeHtml(data.phone) + "</p>" +
        "<p><strong>时间：</strong>" + escapeHtml(formatTime(data.timestamp)) + "</p>" +
        "<p><strong>状态：</strong>" + escapeHtml(data.status) + "</p>" +
        "<hr />" +
        "<div class='detail-content'>" + escapeHtml(data.content) + "</div>" +
        "</div>"
    });
  }

  function sendSMS() {
    var toNum = document.getElementById("ToNumInput").value.trim();
    var textContent = document.getElementById("Text_Content").value;

    if (!toNum || !textContent.trim()) {
      layer.msg("请填写收件人和短信内容！");
      return;
    }

    layer.msg("正在发送短信...");
    fetch(baseUrl + "api/send", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ recipient: toNum, content: textContent })
    })
      .then(function (response) { return response.json(); })
      .then(function (data) {
        if (data.status === "success") {
          layer.msg("发送成功");
          document.getElementById("Text_Content").value = "";
          state.out.page = 1;
          fetchMessages("out");
        } else {
          layer.msg("发送失败：" + (data.error || data.message || ""));
        }
      })
      .catch(function () {
        layer.msg("发送失败");
      });
  }

  function postForm(path, params) {
    return fetch(baseUrl + path, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams(params)
    }).then(function (response) { return response.json(); });
  }

  function deleteSelected(direction) {
    var checkStatus = table.checkStatus(tableId(direction));
    var ids = checkStatus.data.map(function (item) { return item.id; });

    if (!ids.length) {
      layer.msg("请先选择要删除的短信。");
      return;
    }

    layer.confirm("确认删除选中信息？", { icon: 3, btn: ["确定", "取消"] }, function (index) {
      postForm("api/delete", { ids: ids.join(",") })
        .then(function (data) {
          if (data.status === "success") {
            layer.msg("删除成功");
            fetchMessages(direction);
          } else {
            layer.msg("删除失败");
          }
        })
        .catch(function () {
          layer.msg("删除失败");
        });
      layer.close(index);
    });
  }

  function clearMessages(direction) {
    layer.confirm("确认清空所有信息？", { icon: 3, btn: ["确定", "取消"] }, function (index) {
      postForm("api/clear", { direction: direction })
        .then(function (data) {
          if (data.status === "success") {
            layer.msg("短信清空成功");
            state[direction].page = 1;
            fetchMessages(direction);
          } else {
            layer.msg("短信清空失败");
          }
        })
        .catch(function () {
          layer.msg("短信清空失败");
        });
      layer.close(index);
    });
  }

  function bindPager(direction) {
    document.getElementById(direction + "PageSelect").addEventListener("change", function () {
      state[direction].page = parseInt(this.value, 10) || 1;
      fetchMessages(direction);
    });

    document.getElementById(direction + "LimitSelect").addEventListener("change", function () {
      state[direction].limit = parseInt(this.value, 10) || 10;
      state[direction].page = 1;
      fetchMessages(direction);
    });

    document.getElementById(direction + "PrevPage").addEventListener("click", function () {
      if (state[direction].page > 1) {
        state[direction].page--;
        fetchMessages(direction);
      }
    });

    document.getElementById(direction + "NextPage").addEventListener("click", function () {
      if (state[direction].page < totalPages(direction)) {
        state[direction].page++;
        fetchMessages(direction);
      }
    });
  }

  element.on("tab(sms-tabs)", function () {
    var layId = this.getAttribute("lay-id");
    location.hash = "tabid=" + layId;
    if (layId === "in" || layId === "out") {
      fetchMessages(layId);
    }
  });

  table.on("row(inTable)", function (obj) {
    openMessage(obj.data, "in");
  });

  table.on("row(outTable)", function (obj) {
    openMessage(obj.data, "out");
  });

  document.getElementById("sendSMSBtn").onclick = sendSMS;
  document.getElementById("refreshInbox").onclick = function () {
    fetchMessages("in");
    layer.msg("已刷新");
  };
  document.getElementById("refreshSent").onclick = function () {
    fetchMessages("out");
    layer.msg("已刷新");
  };
  document.getElementById("deleteInboxSelected").onclick = function () {
    deleteSelected("in");
  };
  document.getElementById("deleteSentSelected").onclick = function () {
    deleteSelected("out");
  };
  document.getElementById("clearInbox").onclick = function () {
    clearMessages("in");
  };
  document.getElementById("clearSent").onclick = function () {
    clearMessages("out");
  };
  bindPager("in");
  bindPager("out");

  var hashName = "tabid";
  var layid = location.hash.replace(new RegExp("^#" + hashName + "="), "");
  if (layid === "send" || layid === "in" || layid === "out") {
    element.tabChange("sms-tabs", layid);
  }

  fetchMessages("in");
});
